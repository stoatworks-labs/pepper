/**
 * SW Pepper (PG01, an FFGL mixer) and SW Pepper Feed (PG02, an FFGL effect),
 * in a browser.
 *
 * Two pages, one module. `index.html` is SW Pepper: a clear sheet at an angle
 * reflecting a hidden screen over the stage behind it, traced per pixel in
 * linear light. `feed/index.html` is SW Pepper Feed: the cross layout a real
 * pyramid hologram is fed. `<body data-plugin>` says which this page is.
 *
 * What runs here, and what is a port:
 *
 * - **The shaders are the plugin's.** `shaders.js` is spliced from
 *   source/Shaders.cpp by demo/tools/check_shaders.py --write, unedited, and
 *   assembled as Pepper.cpp's and Feed.cpp's InitGL assemble them. The kit's
 *   port() changes the version line and adds precision qualifiers, and
 *   forWebGL() respells the one expression WebGL refuses (a ?: on a struct,
 *   in SW Pepper's trace) as an equivalent call; nothing else. tools/verify.sh
 *   fails if a character drifts, or if that expression changes.
 * - **The CPU half is a port**, between the PORT and FRAME markers below:
 *   Controls.cpp (every conversion from the host's 0..1), Rig.cpp (the rig,
 *   the camera, the foil's ripple modes, with their PCG hash in uint32),
 *   Coating.cpp (the coat's table, the Airy sum in double, with libc++'s
 *   complex formulas), and the CPU half of Pepper.cpp's and Feed.cpp's
 *   constructors, SetFloatParameter and ProcessOpenGL (every uniform). In
 *   double where the C++ is double; Math.fround where it is float, and a
 *   Float32Array holds the parameters as the plugin's float params[] does.
 *   demo/tools/crosscheck/ compares this page's pictures with pgtest's; past
 *   that, only a reader checks it.
 * - Not ported: the clock's unit vote (Timing.cpp; the page hands over its
 *   own clock in seconds), the log (Diag.cpp), the About block, and the
 *   harness's negative controls (the Fault uniform is 0, as shipped).
 */
import { mountDemo } from './vendor/demo.js';
import { GLError, Program } from './vendor/gl.js';
import { SOURCES, SourceRenderer } from './vendor/sources.js';
import * as S from './shaders.js';

const f32 = Math.fround;
const PAGE_PLUGIN = document.body.dataset.plugin === 'feed' ? 'feed' : 'pepper';

// ==== PORT: Controls.cpp =====================================================

const kPi = 3.14159265358979323846;
const kDegree = kPi / 180.0;

const clamp01 = (value) => Math.min(1.0, Math.max(0.0, value));
const clampTo = (value, lo, hi) => Math.min(hi, Math.max(lo, value));
const linear = (t, lo, hi) => lo + (hi - lo) * clamp01(t);
const linearParam = (value, lo, hi) => f32(clamp01((value - lo) / (hi - lo)));
/** lo * ( hi / lo )^t: geometric between two ends. */
const geometric = (t, lo, hi) => lo * Math.pow(hi / lo, clamp01(t));
function geometricParam(value, lo, hi) {
  if (!(value > 0.0)) return 0.0;
  return f32(clamp01(Math.log(value / lo) / Math.log(hi / lo)));
}

/** std::lround: half away from zero, which Math.round is not. */
const lround = (v) => (v < 0 ? -Math.floor(-v + 0.5) : Math.floor(v + 0.5));
function OptionIndex(value, count) {
  return clampTo(lround(value), 0, count - 1);
}

const kSourceStopsMin = -3.0;
const kSourceStopsMax = 6.0;
const kSourceStopsDefault = 2.5;
const kStageStopsMin = -4.0;
const kStageStopsMax = 2.0;
const kStageStopsDefault = 0.0;
const kSourceBlackMax = 0.05;
const kSpillMax = 0.5;
const kPruneLight = 1.0 / 8192.0;

const Polarisation = { None: 0, Horizontal: 1, Vertical: 2, Deg45: 3, Deg135: 4, Count: 5 };

const kViewAngleMinDeg = 40.0;
const kViewAngleMaxDeg = 180.0;
const kViewAngleDefaultDeg = 180.0;

const SourceGainFromParam = (value) => Math.pow(2, linear(value, kSourceStopsMin, kSourceStopsMax));
const StageGainFromParam = (value) => Math.pow(2, linear(value, kStageStopsMin, kStageStopsMax));
const SourceBlackFromParam = (value) => clamp01(value) * kSourceBlackMax;
const SpillFromParam = (value) => clamp01(value) * kSpillMax;
const ParamForSourceStops = (stops) => linearParam(stops, kSourceStopsMin, kSourceStopsMax);
const ParamForStageStops = (stops) => linearParam(stops, kStageStopsMin, kStageStopsMax);

function PolarisationAngle(kind) {
  switch (kind) {
    case Polarisation.Horizontal: return 0.0;
    case Polarisation.Vertical: return 90.0 * kDegree;
    case Polarisation.Deg45: return 45.0 * kDegree;
    case Polarisation.Deg135: return 135.0 * kDegree;
    default: return 0.0;
  }
}

const ViewAngleFromParam = (value) => linear(value, kViewAngleMinDeg, kViewAngleMaxDeg) * kDegree;
const ParamForViewAngle = (radians) => linearParam(radians / kDegree, kViewAngleMinDeg, kViewAngleMaxDeg);

function FalloffPower(fullAngle) {
  // cos^p( half ) = 1/2. At 180 degrees the half angle is 90 and cos is 0:
  // no finite p, and the emitter is Lambertian, p = 0.
  const c = Math.cos(0.5 * fullAngle);
  if (!(c > 1e-9)) return 0.0;
  return Math.log(0.5) / Math.log(c);
}

const RigKind = { Pane: 0, Pyramid: 1, Count: 2 };
const MountKind = { Below: 0, Above: 1, Count: 2 };

const kSheetAngleMinDeg = 25.0;
const kSheetAngleMaxDeg = 65.0;
const kSheetAngleDefaultDeg = 45.0;

const SidesFromParam = (value) => (OptionIndex(value, 2) === 0 ? 4 : 3);

const kCentreMin = 0.1;
const kCentreMax = 0.6;
const kCentreDefault = 0.3;

const ScreenKind = { Feed: 0, AsIs: 1, Count: 2 };

const SheetAngleFromParam = (value) => linear(value, kSheetAngleMinDeg, kSheetAngleMaxDeg) * kDegree;
const CentreFromParam = (value) => linear(value, kCentreMin, kCentreMax);
const ParamForSheetAngle = (radians) => linearParam(radians / kDegree, kSheetAngleMinDeg, kSheetAngleMaxDeg);
const ParamForCentre = (fraction) => linearParam(fraction, kCentreMin, kCentreMax);

const SheetKind = { PetFoil: 0, FloatGlass: 1, Acrylic: 2, Polycarbonate: 3, Count: 4 };
const kChannelNm = [611.3, 549.1, 464.3];
const kRayChannel = 1;

function IndexAt(kind, nanometres) {
  const um = nanometres * 1e-3;
  const um2 = um * um;
  switch (kind) {
    // Drawn polyester film, 1.65 at the d line, a Cauchy slope (Zhang et al. 2020).
    case SheetKind.PetFoil: return 1.65 + 0.0112 * (1.0 / um2 - 1.0 / (0.5876 * 0.5876));
    // Clear soda-lime window glass (Rubin 1985).
    case SheetKind.FloatGlass: return 1.5130 - 0.003169 * um2 + 0.003962 / um2;
    // PMMA and polycarbonate, one-term Sellmeier (Sultanova et al. 2009).
    case SheetKind.Acrylic: return Math.sqrt(1.0 + 1.1819 * um2 / (um2 - 0.011313));
    case SheetKind.Polycarbonate: return Math.sqrt(1.0 + 1.4182 * um2 / (um2 - 0.021304));
    default: return 1.5;
  }
}
const ChannelIndex = (kind, channel) => IndexAt(kind, kChannelNm[clampTo(channel, 0, 2)]);
const SHEET_NAMES = ['PET Foil', 'Float Glass', 'Acrylic', 'Polycarbonate'];

const CoatingKind = { None: 0, Aluminium: 1, TitaniumDioxide: 2, Count: 3 };
const COATING_NAMES = ['None', 'Aluminium', 'Titanium Dioxide'];

const kAluminiumMaxNm = 10.0;
const kTitaniaMaxQuarters = 2.0;
const kCoatThicknessDefault = 0.5;

function CoatIndex(kind, channel) {
  // At 611.3, 549.1 and 464.3 nm: Rakic et al. (1998), bulk aluminium;
  // Siefke et al. (2016), an ALD titania film.
  const kAluminium = [[1.2408, 7.0886], [0.9618, 6.4484], [0.6350, 5.4814]];
  const kTitania = [[2.3992, 4.43e-9], [2.4365, 5.61e-8], [2.5273, 5.42e-6]];
  const c = clampTo(channel, 0, 2);
  if (kind === CoatingKind.Aluminium) return kAluminium[c];
  if (kind === CoatingKind.TitaniumDioxide) return kTitania[c];
  return [1.0, 0.0];
}

function CoatThicknessNm(kind, value) {
  if (kind === CoatingKind.Aluminium) return clamp01(value) * kAluminiumMaxNm;
  if (kind === CoatingKind.TitaniumDioxide) {
    const [n] = CoatIndex(kind, kRayChannel);
    return clamp01(value) * kTitaniaMaxQuarters * kChannelNm[kRayChannel] / (4.0 * n);
  }
  return 0.0;
}

const kThicknessMax = 0.04;
const kRippleMaxDeg = 3.0;
const kRippleDefaultDeg = 0.2;
const kRippleHzMin = 0.05;
const kRippleHzMax = 4.0;
const kRippleHzDefault = 0.5;

const ThicknessFromParam = (value) => clamp01(value) * kThicknessMax;
const RippleFromParam = (value) => clamp01(value) * kRippleMaxDeg * kDegree;
const RippleHzFromParam = (value) => geometric(value, kRippleHzMin, kRippleHzMax);
const ParamForThickness = (rigSizes) => linearParam(rigSizes, 0.0, kThicknessMax);
const ParamForRipple = (radians) => linearParam(radians / kDegree, 0.0, kRippleMaxDeg);
const ParamForRippleHz = (hertz) => geometricParam(hertz, kRippleHzMin, kRippleHzMax);

const kDistanceMin = 1.0;
const kDistanceMax = 12.0;
const kDistanceDefault = 2.5;
const kElevationMinDeg = -20.0;
const kElevationMaxDeg = 60.0;
const kAzimuthMaxDeg = 180.0;
const kFieldOfViewDeg = 30.0;

const DistanceFromParam = (value) => geometric(value, kDistanceMin, kDistanceMax);
const ElevationFromParam = (value) => linear(value, kElevationMinDeg, kElevationMaxDeg) * kDegree;
const AzimuthFromParam = (value) => linear(value, -kAzimuthMaxDeg, kAzimuthMaxDeg) * kDegree;
const ParamForDistance = (rigSizes) => geometricParam(rigSizes, kDistanceMin, kDistanceMax);
const ParamForElevation = (radians) => linearParam(radians / kDegree, kElevationMinDeg, kElevationMaxDeg);
const ParamForAzimuth = (radians) => linearParam(radians / kDegree, -kAzimuthMaxDeg, kAzimuthMaxDeg);

// ==== PORT: Rig.cpp ==========================================================

const vec3 = (x = 0.0, y = 0.0, z = 0.0) => ({ x, y, z });
const vadd = (a, b) => vec3(a.x + b.x, a.y + b.y, a.z + b.z);
const vsub = (a, b) => vec3(a.x - b.x, a.y - b.y, a.z - b.z);
const vmul = (a, k) => vec3(a.x * k, a.y * k, a.z * k);
const vdot = (a, b) => a.x * b.x + a.y * b.y + a.z * b.z;
const vcross = (a, b) => vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
function vnormalize(a) {
  const length = Math.sqrt(vdot(a, a));
  return length > 0.0 ? vmul(a, 1.0 / length) : a;
}

const kModeCount = 6;
const kModeM = [1, 2, 1, 2, 3, 1];
const kModeN = [1, 1, 2, 2, 1, 3];
const kMaxFaces = 4;

const ScreenMap = { PaneFloor: 0, AsIs: 1, Feed: 2, AsIsMirrored: 3, FeedMirrored: 4 };

/** PCG's output permutation, in uint32: Math.imul and >>> 0 are C++'s wraparound. */
function hash32(v) {
  const state = (Math.imul(v >>> 0, 747796405) + 2891336453) >>> 0;
  const word = Math.imul(((state >>> ((state >>> 28) + 4)) ^ state) >>> 0, 277803737) >>> 0;
  return ((word >>> 22) ^ word) >>> 0;
}
const unit = (v) => hash32(v) / 4294967296.0;

/** Picture coordinates (x right, y up) to world (x, -z) on the screen. */
const fromPicture = (px, py) => vec3(px, 0.0, -py);

function newFace() {
  return {
    origin: vec3(), edge: vec3(), up: vec3(), normal: vec3(),
    half: 0.0, spread: 0.0, length: 0.0, width: 0.0,
    amplitude: new Array(kModeCount).fill(0.0),
    hertz: new Array(kModeCount).fill(0.0),
    phase: new Array(kModeCount).fill(0.0),
    height: new Array(kModeCount).fill(0.0),
  };
}

/** The membrane modes of one sheet's frame: amplitudes scaled so every mode's
 *  steepest slope sums to tan( Ripple ), phases reduced in double. */
function ringModes(f, faceIndex, input) {
  const weight = [];
  const slope = [];
  let sum = 0.0;
  for (let k = 0; k < kModeCount; k += 1) {
    const m = kModeM[k];
    const n = kModeN[k];
    const seed = (faceIndex * 97 + k * 13 + 1) >>> 0;
    const sign = (hash32(seed) & 1) ? 1.0 : -1.0;
    weight[k] = sign / (m * m + n * n);
    slope[k] = kPi * Math.sqrt((m / f.width) * (m / f.width) + (n / f.length) * (n / f.length));
    sum += Math.abs(weight[k]) * slope[k];
  }

  const steepest = Math.tan(input.ripple);
  for (let k = 0; k < kModeCount; k += 1) {
    f.amplitude[k] = sum > 0.0 ? steepest * weight[k] / sum : 0.0;
    f.hertz[k] = input.modesLocked ? input.rippleHz : input.rippleHz * slope[k] / slope[0];
    f.phase[k] = 2.0 * kPi * unit((faceIndex * 97 + k * 13 + 50) >>> 0);
    // The phase in double, reduced to one turn BEFORE it meets the offset.
    // JavaScript's % on doubles is C's fmod, exactly.
    const turns = (f.hertz[k] * input.seconds) % 1.0;
    f.height[k] = f.amplitude[k] * Math.cos(2.0 * kPi * turns + f.phase[k]);
  }
}

function PictureAxes(map) {
  switch (map) {
    case ScreenMap.PaneFloor: return [vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0)];
    case ScreenMap.AsIsMirrored:
    case ScreenMap.FeedMirrored: return [vec3(-1.0, 0.0, 0.0), vec3(0.0, 0.0, -1.0)];
    default: return [vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, -1.0)];
  }
}

function FeedArm(sides, centre, aspect) {
  const r0 = 0.5 * centre;
  const w = Math.min(centre * Math.tan(kPi / sides), aspect * (0.5 - r0));
  return [w, w / aspect];
}

function ArmAxes(sides, j) {
  const theta = 2.0 * kPi * j / sides;
  // rot( theta ) applied to (0, -1), and that turned a further 90 degrees.
  const ax = Math.sin(theta);
  const ay = -Math.cos(theta);
  return [ax, ay, -ay, ax];
}

function Build(input) {
  const s = {
    rig: input.rig,
    eye: vec3(), target: vec3(), forward: vec3(), right: vec3(), up: vec3(),
    tanHalfY: 0.0, aspect: 1.0,
    faces: 0,
    face: [newFace(), newFace(), newFace(), newFace()],
    eta: [1.5, 1.5, 1.5], etaRay: 1.5,
    thickness: 0.0, tint: [1.0, 1.0, 1.0],
    coating: 0, coatNm: 0.0, mount: 0,
    map: ScreenMap.PaneFloor, xMin: 0.0, xMax: 0.0, zMin: 0.0, zMax: 0.0,
    polarisation: vec3(), falloff: 0.0, feedFlip: false,
    sides: 4, centre: 0.3, armW: 0.0, armD: 0.0,
    sourceGain: 1.0, sourceBlack: 0.0, spill: 0.0, stageGain: 1.0, linearLight: true,
    rigSize: 1.0, pyramidH: 0.0, sheetAngle: 0.0,
  };
  s.sheetAngle = input.angle;
  for (let c = 0; c < 3; c += 1) s.eta[c] = ChannelIndex(input.sheet, c);
  s.etaRay = s.eta[kRayChannel];
  s.coating = input.coating;
  s.coatNm = input.coating === CoatingKind.None ? 0.0 : input.coatNm;
  s.mount = input.mount;
  s.falloff = FalloffPower(input.viewAngle);
  const above = input.mount === MountKind.Above && !input.aboveAsBelow;
  for (let c = 0; c < 3; c += 1) s.tint[c] = clampTo(input.tint[c], 0.0, 1.0);
  s.sourceGain = input.sourceGain;
  s.sourceBlack = input.sourceBlack;
  s.spill = input.spill;
  s.stageGain = input.stageGain;
  s.linearLight = input.linearLight;
  s.sides = input.sides;
  s.centre = input.centre;
  s.aspect = input.outAspect;

  const cosA = Math.cos(input.angle);
  const sinA = Math.sin(input.angle);
  const A = input.srcAspect;

  if (input.rig === RigKind.Pane) {
    s.rigSize = 1.0;
    s.target = vec3(0.0, 0.5, 0.0);

    s.faces = 1;
    const f = s.face[0];
    f.origin = vec3(0.0, 0.0, 0.0);
    f.edge = vec3(1.0, 0.0, 0.0);
    f.up = vec3(0.0, sinA, cosA);
    f.normal = vcross(f.edge, f.up);
    f.half = 0.5 * A + 1.0;
    f.spread = 0.0;
    f.length = 1.5 / cosA;
    f.width = 2.0 * f.half;
    ringModes(f, 0, input);

    // Overhead, the picture's top goes away from the audience.
    s.map = above ? ScreenMap.AsIs : ScreenMap.PaneFloor;
    s.xMin = -0.5 * A;
    s.xMax = 0.5 * A;
    s.zMin = 0.0;
    s.zMax = 1.0;
  } else {
    const n = input.sides;
    const r0 = 0.5 * input.centre;
    const tn = Math.tan(kPi / n);
    const top = 2.0 * 0.5 * tn; // the top edge's length
    s.rigSize = 0.6 * top;
    s.pyramidH = (0.5 - r0) * Math.tan(input.angle);
    s.target = vec3(0.0, 0.5 * s.pyramidH, 0.0);

    s.faces = n;
    for (let j = 0; j < n; j += 1) {
      const [ax, ay, tx, ty] = ArmAxes(n, j);
      const outward = fromPicture(ax, ay);
      const f = s.face[j];
      f.origin = vmul(outward, r0);
      f.edge = fromPicture(tx, ty);
      f.up = vadd(vmul(outward, cosA), vec3(0.0, sinA, 0.0));
      f.normal = vcross(f.edge, f.up);
      f.half = r0 * tn;
      f.spread = cosA * tn;
      f.length = (0.5 - r0) / cosA;
      f.width = top;
      ringModes(f, j, input);
    }

    const asIs = input.screen === ScreenKind.AsIs;
    if (above) s.map = asIs ? ScreenMap.AsIsMirrored : ScreenMap.FeedMirrored;
    else s.map = asIs ? ScreenMap.AsIs : ScreenMap.Feed;
    s.feedFlip = above;
    // The picture's short side is the unit.
    const halfX = A >= 1.0 ? 0.5 * A : 0.5;
    const halfZ = A >= 1.0 ? 0.5 : 0.5 / A;
    s.xMin = -halfX;
    s.xMax = halfX;
    s.zMin = -halfZ;
    s.zMax = halfZ;
  }

  if (input.polarisation !== Polarisation.None) {
    const [px, py] = PictureAxes(s.map);
    const chi = PolarisationAngle(input.polarisation) * (input.clockwise ? -1.0 : 1.0);
    s.polarisation = vadd(vmul(px, Math.cos(chi)), vmul(py, Math.sin(chi)));
  }

  [s.armW, s.armD] = FeedArm(input.sides, input.centre, A);
  s.thickness = input.thickness * s.rigSize;

  // The camera, on an orbit round the rig's centre.
  const D = input.distance * s.rigSize;
  const ce = Math.cos(input.elevation);
  s.eye = vadd(s.target, vec3(D * ce * Math.sin(input.azimuth), D * Math.sin(input.elevation), D * ce * Math.cos(input.azimuth)));
  s.forward = vnormalize(vsub(s.target, s.eye));
  s.right = vnormalize(vcross(s.forward, vec3(0.0, 1.0, 0.0)));
  s.up = vcross(s.right, s.forward);
  s.tanHalfY = Math.tan(0.5 * kFieldOfViewDeg * kPi / 180.0);

  if (input.mount === MountKind.Above) {
    // The camera, reflected into the Below rig (Rig.h): y negated about the
    // rig's centre.
    s.eye.y = 2.0 * s.target.y - s.eye.y;
    s.forward.y = -s.forward.y;
    s.right.y = -s.right.y;
    s.up.y = -s.up.y;
  }
  return s;
}

// ==== PORT: Coating.cpp ======================================================
//
// Complex numbers as [re, im], with libc++'s formulas: sqrt is
// polar( sqrt( abs ), arg / 2 ), exp is polar( exp( re ), im ), division is
// the scaled C99 one, which equals the plain quotient barring overflow.

const cx = (re, im = 0.0) => [re, im];
const cadd = (a, b) => [a[0] + b[0], a[1] + b[1]];
const csub = (a, b) => [a[0] - b[0], a[1] - b[1]];
const cneg = (a) => [-a[0], -a[1]];
const cmul = (a, b) => [a[0] * b[0] - a[1] * b[1], a[0] * b[1] + a[1] * b[0]];
const cscale = (a, k) => [a[0] * k, a[1] * k];
const cconj = (a) => [a[0], -a[1]];
const cnorm = (a) => a[0] * a[0] + a[1] * a[1];
function cdiv(a, b) {
  const denom = b[0] * b[0] + b[1] * b[1];
  return [(a[0] * b[0] + a[1] * b[1]) / denom, (a[1] * b[0] - a[0] * b[1]) / denom];
}
function polar(rho, theta) {
  return [rho * Math.cos(theta), rho * Math.sin(theta)];
}
const csqrt = (a) => polar(Math.sqrt(Math.hypot(a[0], a[1])), Math.atan2(a[1], a[0]) / 2.0);
function cexp(a) {
  if (a[1] === 0) return [Math.exp(a[0]), a[1]];
  const e = Math.exp(a[0]);
  return [e * Math.cos(a[1]), e * Math.sin(a[1])];
}

const coat = { kSize: 1024, kRows: 12 };

/** N cos( theta ) in a medium of index N, for Snell's invariant S. */
const normalPart = (N, S) => csqrt(csub(cmul(N, N), cx(S * S)));

/** Born & Wolf's interface coefficients from medium i into j. */
function across(Ni, xi, Nj, xj) {
  const NjNjxi = cmul(cmul(Nj, Nj), xi);
  const NiNixj = cmul(cmul(Ni, Ni), xj);
  return {
    rs: cdiv(csub(xi, xj), cadd(xi, xj)),
    ts: cdiv(cscale(xi, 2.0), cadd(xi, xj)),
    rp: cdiv(csub(NjNjxi, NiNixj), cadd(NjNjxi, NiNixj)),
    tp: cdiv(cmul(cmul(cscale(Ni, 2.0), Nj), xi), cadd(NjNjxi, NiNixj)),
  };
}

function coatAt(cosiIn, coatN, nanometres, sheetN, lambdaNm) {
  const cosi = clampTo(cosiIn, 0.0, 1.0);
  const S = Math.sqrt(Math.max(0.0, 1.0 - cosi * cosi));
  const N0 = cx(1.0);
  const Ns = cx(sheetN);
  const x0 = cx(cosi);
  const x1 = normalPart(coatN, S);
  const xs = normalPart(Ns, S);

  const a = across(N0, x0, coatN, x1); // air into the coat
  const b = across(coatN, x1, Ns, xs); // the coat into the sheet

  // The layer's round trip, exp( 2 i beta ), beta = 2 pi d N cos / lambda.
  const beta = cscale(x1, 2.0 * 3.14159265358979323846 * nanometres / lambdaNm);
  const one = cexp(cmul(cx(0.0, 1.0), beta));
  const E = cmul(one, one);

  // Airy: every echo inside the coat, summed in amplitude.
  const ds = cadd(cx(1.0), cmul(cmul(a.rs, b.rs), E));
  const dp = cadd(cx(1.0), cmul(cmul(a.rp, b.rp), E));
  const rs = cdiv(cadd(a.rs, cmul(b.rs, E)), ds);
  const rp = cdiv(cadd(a.rp, cmul(b.rp, E)), dp);
  const ris = cdiv(csub(cneg(b.rs), cmul(a.rs, E)), ds);
  const rip = cdiv(csub(cneg(b.rp), cmul(a.rp, E)), dp);
  const norm = cosi > 0.0 ? Math.sqrt(xs[0] / x0[0]) : 0.0;
  const ts = cscale(cdiv(cmul(cmul(a.ts, b.ts), one), ds), norm);
  const tp = cscale(cdiv(cmul(cmul(a.tp, b.tp), one), dp), norm);

  const c = cmul(cconj(rs), rp);
  const ci = cmul(cconj(ris), rip);
  const g = cmul(cconj(ts), tp);
  return [cnorm(rs), cnorm(rp), cnorm(ris), cnorm(rip), cnorm(ts), cnorm(tp), c[0], c[1], ci[0], ci[1], g[0], g[1]];
}

/** Row r of kSize texels holds quantity r for the three channels; alpha 0. */
function BuildTable(kind, nanometres, sheetN, rgba) {
  rgba.fill(0.0);
  for (let c = 0; c < 3; c += 1) {
    const N = CoatIndex(kind, c);
    for (let i = 0; i < coat.kSize; i += 1) {
      const v = coatAt(i / (coat.kSize - 1), cx(N[0], N[1]), nanometres, sheetN[c], kChannelNm[c]);
      for (let r = 0; r < coat.kRows; r += 1) rgba[(r * coat.kSize + i) * 4 + c] = v[r];
    }
  }
}

// ==== END PORT ===============================================================

// ==== FRAME: Pepper.cpp and Feed.cpp, the CPU half ===========================

const PT = {
  LINEAR_LIGHT: 0, OPACITY: 1, SOURCE_LEVEL: 2, SOURCE_BLACK: 3, POLARISATION: 4, VIEW_ANGLE: 5,
  STAGE_LEVEL: 6, SPILL: 7,
  RIG: 8, MOUNT: 9, SHEET_ANGLE: 10, SIDES: 11, CENTRE: 12, SCREEN: 13,
  SHEET: 14, COATING: 15, COAT_THICKNESS: 16, THICKNESS: 17, TINT_R: 18, TINT_G: 19, TINT_B: 20,
  RIPPLE: 21, RIPPLE_SPEED: 22,
  DISTANCE: 23, ELEVATION: 24, AZIMUTH: 25,
  COUNT: 26, // the About block follows in the plugin; it is not here
};

const FT = { SIDES: 0, CENTRE: 1, MOUNT: 2, COUNT: 3 };

/** Pepper's declaration, in index order: name, FF type, elements, group. */
const PEPPER_TABLE = [
  [PT.LINEAR_LIGHT, 'Linear Light', 'boolean', null, 'Light'],
  [PT.OPACITY, 'Opacity', 'standard', null, 'Light'],
  [PT.SOURCE_LEVEL, 'Source Level', 'standard', null, 'Light'],
  [PT.SOURCE_BLACK, 'Source Black', 'standard', null, 'Light'],
  [PT.POLARISATION, 'Polarisation', 'option', ['None', 'Horizontal', 'Vertical', '45 Degrees', '135 Degrees'], 'Light'],
  [PT.VIEW_ANGLE, 'Viewing Angle', 'standard', null, 'Light'],
  [PT.STAGE_LEVEL, 'Stage Level', 'standard', null, 'Light'],
  [PT.SPILL, 'Spill', 'standard', null, 'Light'],
  [PT.RIG, 'Rig', 'option', ['Pane', 'Pyramid'], 'Rig'],
  [PT.MOUNT, 'Mount', 'option', ['Screen Below', 'Screen Above'], 'Rig'],
  [PT.SHEET_ANGLE, 'Sheet Angle', 'standard', null, 'Rig'],
  [PT.SIDES, 'Sides', 'option', ['Four', 'Three'], 'Rig'],
  [PT.CENTRE, 'Centre', 'standard', null, 'Rig'],
  [PT.SCREEN, 'Screen', 'option', ['Feed', 'As Is'], 'Rig'],
  [PT.SHEET, 'Sheet', 'option', SHEET_NAMES, 'Sheet'],
  [PT.COATING, 'Coating', 'option', COATING_NAMES, 'Sheet'],
  [PT.COAT_THICKNESS, 'Coat Thickness', 'standard', null, 'Sheet'],
  [PT.THICKNESS, 'Thickness', 'standard', null, 'Sheet'],
  [PT.TINT_R, 'Tint', 'red', null, 'Sheet'],
  [PT.TINT_G, 'Tint_Green', 'green', null, 'Sheet'],
  [PT.TINT_B, 'Tint_Blue', 'blue', null, 'Sheet'],
  [PT.RIPPLE, 'Ripple', 'standard', null, 'Sheet'],
  [PT.RIPPLE_SPEED, 'Ripple Speed', 'standard', null, 'Sheet'],
  [PT.DISTANCE, 'Distance', 'standard', null, 'View'],
  [PT.ELEVATION, 'Elevation', 'standard', null, 'View'],
  [PT.AZIMUTH, 'Azimuth', 'standard', null, 'View'],
].map(([slot, name, kind, elements, group]) => ({ slot, name, kind, elements, group }));

const FEED_TABLE = [
  [FT.SIDES, 'Sides', 'option', ['Four', 'Three'], 'Feed'],
  [FT.CENTRE, 'Centre', 'standard', null, 'Feed'],
  [FT.MOUNT, 'Mount', 'option', ['Screen Below', 'Screen Above'], 'Feed'],
].map(([slot, name, kind, elements, group]) => ({ slot, name, kind, elements, group }));

function bindUnit(gl, unit, texture) {
  gl.activeTexture(gl.TEXTURE0 + unit);
  gl.bindTexture(gl.TEXTURE_2D, texture);
}

/**
 * Pepper::Pepper's defaults, which SetParamInfof reads back as the declared
 * ones. A Float32Array, as the plugin's float params[] is.
 */
function PepperDefaults() {
  const p = new Float32Array(PT.COUNT);
  p[PT.LINEAR_LIGHT] = 1.0;
  p[PT.OPACITY] = 1.0;
  p[PT.SOURCE_LEVEL] = ParamForSourceStops(kSourceStopsDefault);
  p[PT.SOURCE_BLACK] = 0.0;
  p[PT.POLARISATION] = 0.0;
  p[PT.VIEW_ANGLE] = ParamForViewAngle(kViewAngleDefaultDeg * kPi / 180.0);
  p[PT.STAGE_LEVEL] = ParamForStageStops(kStageStopsDefault);
  p[PT.SPILL] = 0.0;
  p[PT.RIG] = 0.0;
  p[PT.MOUNT] = 0.0;
  p[PT.SHEET_ANGLE] = ParamForSheetAngle(kSheetAngleDefaultDeg * kPi / 180.0);
  p[PT.SIDES] = 0.0;
  p[PT.CENTRE] = ParamForCentre(kCentreDefault);
  p[PT.SCREEN] = 0.0;
  p[PT.SHEET] = 0.0;
  p[PT.COATING] = 0.0;
  p[PT.COAT_THICKNESS] = kCoatThicknessDefault;
  p[PT.THICKNESS] = 0.0;
  p[PT.TINT_R] = 1.0;
  p[PT.TINT_G] = 1.0;
  p[PT.TINT_B] = 1.0;
  p[PT.RIPPLE] = ParamForRipple(kRippleDefaultDeg * kPi / 180.0);
  p[PT.RIPPLE_SPEED] = ParamForRippleHz(kRippleHzDefault);
  p[PT.DISTANCE] = ParamForDistance(kDistanceDefault);
  p[PT.ELEVATION] = ParamForElevation(0.0);
  p[PT.AZIMUTH] = ParamForAzimuth(0.0);
  return p;
}

/** Feed::Feed's. */
function FeedDefaults() {
  const p = new Float32Array(FT.COUNT);
  p[FT.SIDES] = 0.0;
  p[FT.CENTRE] = ParamForCentre(kCentreDefault);
  p[FT.MOUNT] = 0.0;
  return p;
}

/** ProcessOpenGL's rig::Inputs, from the parameters, as Pepper.cpp fills them. */
function PepperInputs(P, srcAspect, outAspect, seconds) {
  const input = {
    rig: OptionIndex(P[PT.RIG], RigKind.Count),
    angle: SheetAngleFromParam(P[PT.SHEET_ANGLE]),
    sides: SidesFromParam(P[PT.SIDES]),
    centre: CentreFromParam(P[PT.CENTRE]),
    screen: OptionIndex(P[PT.SCREEN], ScreenKind.Count),
    sheet: OptionIndex(P[PT.SHEET], SheetKind.Count),
    thickness: ThicknessFromParam(P[PT.THICKNESS]),
    tint: [P[PT.TINT_R], P[PT.TINT_G], P[PT.TINT_B]],
    ripple: RippleFromParam(P[PT.RIPPLE]),
    rippleHz: RippleHzFromParam(P[PT.RIPPLE_SPEED]),
    distance: DistanceFromParam(P[PT.DISTANCE]),
    elevation: ElevationFromParam(P[PT.ELEVATION]),
    azimuth: AzimuthFromParam(P[PT.AZIMUTH]),
  };
  const opacity = clampTo(P[PT.OPACITY], 0.0, 1.0);
  input.sourceGain = SourceGainFromParam(P[PT.SOURCE_LEVEL]) * opacity;
  input.sourceBlack = SourceBlackFromParam(P[PT.SOURCE_BLACK]);
  input.spill = SpillFromParam(P[PT.SPILL]);
  input.stageGain = StageGainFromParam(P[PT.STAGE_LEVEL]);
  input.polarisation = OptionIndex(P[PT.POLARISATION], Polarisation.Count);
  input.viewAngle = ViewAngleFromParam(P[PT.VIEW_ANGLE]);
  input.mount = OptionIndex(P[PT.MOUNT], MountKind.Count);
  input.coating = OptionIndex(P[PT.COATING], CoatingKind.Count);
  input.coatNm = CoatThicknessNm(input.coating, P[PT.COAT_THICKNESS]);
  // The harness's negative controls, off as shipped.
  input.aboveAsBelow = false;
  input.clockwise = false;
  input.modesLocked = false;
  input.linearLight = P[PT.LINEAR_LIGHT] > 0.5;
  input.srcAspect = srcAspect;
  input.outAspect = outAspect;
  input.seconds = seconds;
  return [input, opacity];
}

/** SW Pepper: Pepper::Pepper, SetFloatParameter, ProcessOpenGL, UpdateCoatTable. */
class PepperInstance {
  constructor(gl, quad, shader) {
    this.gl = gl;
    this.quad = quad;
    this.shader = shader;
    this.params = PepperDefaults();

    // InitGL's coat table: float texels read with texelFetch, so NEAREST and
    // no mipmaps (no float-filtering extension is needed).
    this.coatTexture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this.coatTexture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, coat.kSize, coat.kRows, 0, gl.RGBA, gl.FLOAT, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.bindTexture(gl.TEXTURE_2D, null);
    this.coatData = new Float32Array(coat.kSize * coat.kRows * 4);
    this.coatKey = [-1, -1, -1];
    this.coatKeyNm = 0.0;
    this.lastSetup = null;
  }

  SetFloatParameter(index, value) {
    if (index >= 0 && index < PT.COUNT) this.params[index] = value;
  }

  /** dest and src are {texture, width, height}; seconds is the clock. */
  ProcessOpenGL(dest, src, outW, outH, seconds) {
    const gl = this.gl;
    const P = this.params;
    const shader = this.shader;

    //-----------------------------------------------------------------
    // The rig, as numbers. In double here; the shader gets floats.
    //-----------------------------------------------------------------
    const [input, opacity] = PepperInputs(P, src.width / src.height, outW / outH, seconds);
    const s = Build(input);
    this.lastSetup = s;

    //-----------------------------------------------------------------
    // Both inputs on units 0 and 1, the coat's table on unit 2.
    //-----------------------------------------------------------------
    shader.use();
    bindUnit(gl, 0, dest.texture);
    bindUnit(gl, 1, src.texture);
    bindUnit(gl, 2, this.coatTexture);
    this.UpdateCoatTable(s);

    shader.setSampler('TextureDest', 0);
    shader.setSampler('TextureSrc', 1);
    shader.setSampler('CoatTable', 2);
    shader.setInt('Coated', s.coating !== CoatingKind.None ? 1 : 0);

    // GetMaxGLTexCoords: a browser texture is never padded, so both are 1.
    shader.set('MaxUVDest', 1.0, 1.0);
    shader.set('MaxUVSrc', 1.0, 1.0);
    shader.set('HalfTexelDest', f32(0.5 / dest.width), f32(0.5 / dest.height));
    shader.set('HalfTexelSrc', f32(0.5 / src.width), f32(0.5 / src.height));
    shader.set('FoldMaxUV', 1.0, 1.0);
    gl.uniform2i(shader.location('OutSize'), outW, outH);

    shader.setInt('RigKind', s.rig);
    shader.set('Eye', s.eye.x, s.eye.y, s.eye.z);
    shader.set('Forward', s.forward.x, s.forward.y, s.forward.z);
    shader.set('CamRight', s.right.x, s.right.y, s.right.z);
    shader.set('CamUp', s.up.x, s.up.y, s.up.z);
    shader.set('TanHalf', s.tanHalfY * s.aspect, s.tanHalfY);

    const origin = new Float32Array(12);
    const edge = new Float32Array(12);
    const up = new Float32Array(12);
    const normal = new Float32Array(12);
    const bounds = new Float32Array(16);
    const heights = new Float32Array(kMaxFaces * kModeCount);
    let rippling = false;
    for (let j = 0; j < s.faces; j += 1) {
      const f = s.face[j];
      const v = [f.origin, f.edge, f.up, f.normal];
      const out = [origin, edge, up, normal];
      for (let a = 0; a < 4; a += 1) {
        out[a][3 * j + 0] = v[a].x;
        out[a][3 * j + 1] = v[a].y;
        out[a][3 * j + 2] = v[a].z;
      }
      bounds[4 * j + 0] = f.half;
      bounds[4 * j + 1] = f.spread;
      bounds[4 * j + 2] = f.length;
      bounds[4 * j + 3] = f.width;
      for (let k = 0; k < kModeCount; k += 1) {
        heights[kModeCount * j + k] = f.height[k];
        rippling = rippling || f.amplitude[k] !== 0.0;
      }
    }
    shader.setInt('FaceCount', s.faces);
    shader.setArray('FaceOrigin', origin, 3);
    shader.setArray('FaceEdge', edge, 3);
    shader.setArray('FaceUp', up, 3);
    shader.setArray('FaceNormal', normal, 3);
    shader.setArray('FaceBounds', bounds, 4);
    shader.setArray('ModeHeight', heights, 1);
    shader.setInt('Rippling', rippling ? 1 : 0);

    shader.setInt('ScreenMap', s.map);
    shader.set('ScreenRect', s.xMin, s.xMax, s.zMin, s.zMax);
    shader.set('SourceE', s.polarisation.x, s.polarisation.y, s.polarisation.z);
    shader.set('Falloff', s.falloff);
    shader.setInt('FeedFlip', s.feedFlip ? 1 : 0);

    const axes = new Float32Array(16);
    for (let j = 0; j < s.sides; j += 1) axes.set(ArmAxes(s.sides, j), 4 * j);
    shader.setInt('FeedSides', s.sides);
    shader.set('FeedR0', 0.5 * s.centre);
    shader.set('FeedArm', s.armW, s.armD);
    shader.setArray('FeedAxis', axes, 4);

    shader.set('Eta', s.eta[0], s.eta[1], s.eta[2]);
    shader.set('EtaRay', s.etaRay);
    shader.set('Thickness', s.thickness);
    shader.set('Tint', s.tint[0], s.tint[1], s.tint[2]);

    shader.set('SourceGain', s.sourceGain);
    shader.set('SourceBlack', s.sourceBlack);
    shader.set('Spill', s.spill);
    shader.set('StageGain', s.stageGain);
    shader.set('Opacity', opacity);
    shader.setInt('LinearLight', s.linearLight ? 1 : 0);
    // From Source Level, NOT Source Level x Opacity: which rays are followed
    // must not depend on the fader (pgtest --opacity).
    const brightest = Math.max(SourceGainFromParam(P[PT.SOURCE_LEVEL]), s.spill, s.stageGain, 1e-6);
    shader.set('Prune', kPruneLight / brightest);

    const c45 = Math.sqrt(0.5);
    shader.set('Normal45', 0.0, -c45, c45);
    shader.setInt('Fault', 0);

    this.quad.draw();

    for (const u of [2, 1, 0]) bindUnit(gl, u, null);
  }

  UpdateCoatTable(s) {
    if (s.coating === CoatingKind.None) return;
    const gl = this.gl;
    const incoherent = 0;
    const sheet = OptionIndex(this.params[PT.SHEET], SheetKind.Count);
    const nm = f32(s.coatNm);
    const key = this.coatKey;
    if (key[0] === s.coating && key[1] === sheet && key[2] === incoherent && this.coatKeyNm === nm) return;
    BuildTable(s.coating, s.coatNm, s.eta, this.coatData);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, coat.kSize, coat.kRows, gl.RGBA, gl.FLOAT, this.coatData);
    this.coatKey = [s.coating, sheet, incoherent];
    this.coatKeyNm = nm;
  }

  dispose() {
    this.gl.deleteTexture(this.coatTexture);
  }
}

/** SW Pepper Feed: Feed::Feed, SetFloatParameter, ProcessOpenGL. */
class FeedInstance {
  constructor(gl, quad, shader) {
    this.gl = gl;
    this.quad = quad;
    this.shader = shader;
    this.params = FeedDefaults();
    this.last = null;
  }

  SetFloatParameter(index, value) {
    if (index >= 0 && index < FT.COUNT) this.params[index] = value;
  }

  ProcessOpenGL(input, outW, outH) {
    const gl = this.gl;
    const shader = this.shader;
    const sides = SidesFromParam(this.params[FT.SIDES]);
    const centre = CentreFromParam(this.params[FT.CENTRE]);
    const aspect = input.width / input.height;
    const [w, d] = FeedArm(sides, centre, aspect);
    const axes = new Float32Array(16);
    for (let j = 0; j < sides; j += 1) axes.set(ArmAxes(sides, j), 4 * j);
    const above = OptionIndex(this.params[FT.MOUNT], MountKind.Count) === MountKind.Above;
    this.last = { sides, centre, w, d, above };

    shader.use();
    bindUnit(gl, 0, input.texture);
    shader.setSampler('InputTexture', 0);
    shader.set('MaxUV', 1.0, 1.0);
    shader.set('HalfTexel', f32(0.5 / input.width), f32(0.5 / input.height));
    shader.set('FoldMaxUV', 1.0, 1.0);
    gl.uniform2i(shader.location('OutSize'), outW, outH);
    shader.set('ShortSide', Math.min(outW, outH));
    shader.setInt('FeedSides', sides);
    shader.setInt('FeedFlip', above ? 1 : 0);
    shader.set('FeedR0', 0.5 * centre);
    shader.set('FeedArm', w, d);
    shader.setArray('FeedAxis', axes, 4);
    shader.setInt('Fault', 0);
    this.quad.draw();
    bindUnit(gl, 0, null);
  }

  dispose() {}
}

// ==== END FRAME ==============================================================

/**
 * The one change this page makes to the plugin's GLSL, and why.
 *
 * WebGL refuses `?:` on a struct. ANGLE's validator says "ternary operator is
 * not allowed for structures in ESSL 1.0 and webgl"; desktop GL 4.1 and plain
 * ES 3.00 both allow it, and SW Pepper's trace() uses one: a thick sheet's
 * k-th back-surface image hands interact() `k < 3 ? W : cOver( W, echo )`
 * (the third carries the rest of the series). Only that
 * expression is respelled, as a call to a helper added just before trace()
 * that returns the operand the ternary would pick. Both operands are
 * evaluated (cOver is pure, so the unpicked one changes nothing); the picked
 * one is the same expression, computed the same way.
 *
 * Each text must occur exactly once or the page refuses to run, and
 * demo/tools/check_shaders.py checks both against source/Shaders.cpp, so a
 * change to that line in the plugin fails verify rather than slipping past.
 * The page's disclosure says so. SW Pepper Feed's shader compiles as it is.
 */
const WEBGL_STRUCT_TERNARY = 'k < 3 ? W : cOver( W, echo )';
const WEBGL_STRUCT_PICK = 'demoPickCoef( k < 3, W, cOver( W, echo ) )';
const WEBGL_PICK_BEFORE = 'void trace( vec3 eye, vec3 dir, inout vec3 light, inout vec3 destW, inout float screenW )';
const WEBGL_PICK_HELPER = [
  '//Added by demo/plugin.js, not the plugin: WebGL refuses ?: on a struct.',
  'Coef demoPickCoef( bool c, Coef a, Coef b )',
  '{',
  '\tif( c )',
  '\t\treturn a;',
  '\treturn b;',
  '}',
  '',
  '',
].join('\n');

function forWebGL(fragment) {
  for (const text of [WEBGL_STRUCT_TERNARY, WEBGL_PICK_BEFORE]) {
    const count = fragment.split(text).length - 1;
    if (count !== 1) {
      throw new GLError(`SW Pepper's fragment shader holds \`${text}\` ${count} times, not once: the page's one WebGL respelling no longer fits the plugin's shader, so it will not run a guess.`);
    }
  }
  return fragment
    .replace(WEBGL_PICK_BEFORE, () => WEBGL_PICK_HELPER + WEBGL_PICK_BEFORE)
    .replace(WEBGL_STRUCT_TERNARY, () => WEBGL_STRUCT_PICK);
}

/**
 * InitGL, both plugins: the vertex stage is AssembleVertex() = kVersion +
 * kVertex, each fragment stage AssembleFragment( body ) = kVersion + kCommon
 * + body. Compiled when first wanted, so the feed's page never pays for the
 * trace.
 */
function programCache(gl) {
  const vertex = S.VERSION + S.VERTEX;
  const assemble = (body) => S.VERSION + S.COMMON + body;
  const compile = {
    pepper: () => new Program(gl, vertex, forWebGL(assemble(S.PEPPER_FRAGMENT)), 'SW Pepper'),
    feed: () => new Program(gl, vertex, assemble(S.FEED_FRAGMENT), 'SW Pepper Feed'),
  };
  const made = {};
  return (which) => {
    if (!made[which]) made[which] = compile[which]();
    return made[which];
  };
}

/** The uniforms a program declares that nothing has set: each would sit at 0. */
function unsetUniforms(gl, program) {
  const set = new Set([...program.locations].filter(([, loc]) => loc !== null).map(([name]) => name));
  const out = [];
  const count = gl.getProgramParameter(program.program, gl.ACTIVE_UNIFORMS);
  for (let i = 0; i < count; i += 1) {
    const name = gl.getActiveUniform(program.program, i).name;
    if (!set.has(name) && !set.has(name.replace(/\[0\]$/, ''))) out.push(name);
  }
  return out;
}

/** An RGBA8 picture given top row first, uploaded as a host would hand it over. */
function uploadFrame(gl, rgba, width, height) {
  const texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA8, width, height);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  const row = width * 4;
  const flipped = new Uint8Array(row * height);
  for (let y = 0; y < height; y += 1) flipped.set(rgba.subarray(y * row, (y + 1) * row), (height - 1 - y) * row);
  gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
  gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, flipped);
  gl.bindTexture(gl.TEXTURE_2D, null);
  return { texture, width, height };
}

const tableFor = (defaults, table) => table.map((row) => ({
  name: row.name,
  kind: row.kind,
  value: defaults[row.slot],
  elements: row.elements ?? [],
}));

/**
 * For demo/tools/crosscheck/: a FRESH instance (pgtest's is one too), the
 * settings by the plugin's display name (an option by its element's name),
 * one frame of the given RGBA8 pictures (top row first, as `pgtest --pipe`
 * reads them), read back the same way. The draw and the read are one task,
 * so the page's own frames cannot interleave; the next one puts the canvas
 * back.
 */
function installHook(gl, quad, program) {
  window.pepperDemo = {
    table: { pepper: tableFor(PepperDefaults(), PEPPER_TABLE), feed: tableFor(FeedDefaults(), FEED_TABLE) },
    renderCase({ plugin = 'pepper', settings = {}, width, height, dest, src, srcWidth, srcHeight, seconds = 0 }) {
      const shader = program(plugin);
      const table = plugin === 'feed' ? FEED_TABLE : PEPPER_TABLE;
      const run = plugin === 'feed' ? new FeedInstance(gl, quad, shader) : new PepperInstance(gl, quad, shader);
      const made = [];
      try {
        for (const [name, value] of Object.entries(settings)) {
          const info = table.find((row) => row.name === name);
          if (!info) throw new Error(`no parameter called '${name}'`);
          const index = info.elements ? info.elements.indexOf(value) : -1;
          const number = index >= 0 ? index : Number(value);
          if (!Number.isFinite(number)) throw new Error(`'${value}' is not a number or an option of ${name}`);
          run.SetFloatParameter(info.slot, number);
        }
        const srcTex = uploadFrame(gl, src, srcWidth, srcHeight);
        made.push(srcTex.texture);
        const canvas = gl.canvas;
        canvas.width = width;
        canvas.height = height;
        let destTex = null;
        if (plugin !== 'feed') {
          destTex = uploadFrame(gl, dest, width, height);
          made.push(destTex.texture);
        }
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, width, height);
        gl.disable(gl.BLEND);
        gl.clearColor(0, 0, 0, 0);
        gl.clear(gl.COLOR_BUFFER_BIT);
        if (plugin === 'feed') run.ProcessOpenGL(srcTex, width, height);
        else run.ProcessOpenGL(destTex, srcTex, width, height, seconds);
        const row = width * 4;
        const read = new Uint8Array(row * height);
        gl.pixelStorei(gl.PACK_ALIGNMENT, 1);
        gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, read);
        const pixels = new Uint8Array(row * height);
        for (let y = 0; y < height; y += 1) pixels.set(read.subarray(y * row, (y + 1) * row), (height - 1 - y) * row);
        return { pixels, missing: [...shader.missing], unset: unsetUniforms(gl, shader), error: gl.getError() };
      } finally {
        for (const t of made) gl.deleteTexture(t);
        run.dispose();
      }
    },
  };
}

//===========================================================================
// The parameters: each plugin's own, in its declaration's order and groups,
// with its names, element lists and defaults. Hints from the README.
//===========================================================================
const V = (v) => f32(v);
const signed = (n, d = 1) => `${n >= 0 ? '+' : '−'}${Math.abs(n).toFixed(d)}`;
const deg = (radians) => radians / kDegree;
const stops = (gain) => Math.log2(gain);

let livePage = null; // the mounted Params, for a readout that depends on another control

function coatText(v) {
  const kind = livePage ? OptionIndex(livePage.get('coating'), CoatingKind.Count) : CoatingKind.None;
  if (kind === CoatingKind.Aluminium) return `${CoatThicknessNm(kind, V(v)).toFixed(2)} nm`;
  if (kind === CoatingKind.TitaniumDioxide) {
    const nm = CoatThicknessNm(kind, V(v));
    return `${(nm * 4.0 * CoatIndex(kind, kRayChannel)[0] / kChannelNm[kRayChannel]).toFixed(2)} λ/4, ${nm.toFixed(1)} nm`;
  }
  return 'no coat';
}

/** id, slot, display, hint. */
const PEPPER_SPEC = [
  ['linearLight', PT.LINEAR_LIGHT, null, 'On: the stage and the screen are decoded from sRGB, added as light and encoded again. Off adds in gamma space, for comparison with Resolume’s Add. Parameter 0: Resolume Arena does not show a mixer’s first parameter, so in Arena this stays on. A browser does not hide it.'],
  ['opacity', PT.OPACITY, (v) => `×${clamp01(V(v)).toFixed(2)}`, 'The hidden screen’s light. In Resolume Arena a mixer parameter named Opacity is bound to the layer’s opacity fader, so fading the layer up brings the ghost in and the glass stays when it is down. Here it is a slider.'],
  ['sourceLevel', PT.SOURCE_LEVEL, (v) => `${signed(linear(V(v), kSourceStopsMin, kSourceStopsMax), 2)} stops`, 'How bright the hidden screen is, −3 to +6 stops of the stage’s white. A ghost needs a screen far brighter than the stage: the sheet reflects only a few per cent of it.'],
  ['sourceBlack', PT.SOURCE_BLACK, (v) => `${(SourceBlackFromParam(V(v)) * 100).toFixed(2)} %`, 'The screen’s own black, up to 5 % of its white: an LED wall or a projector is never quite dark, and the sheet reflects that too.'],
  ['polarisation', PT.POLARISATION, null, 'The screen’s light: unpolarised (an LED wall, a projector), or polarised along the picture’s x, its y or a diagonal, as an LCD is. Reflected light is polarised by the sheet, so a polarised screen can dim or vanish at Brewster’s angle.'],
  ['viewingAngle', PT.VIEW_ANGLE, (v) => { const a = ViewAngleFromParam(V(v)); const p = FalloffPower(a); return p > 0 ? `${deg(a).toFixed(0)}°, cos^${p.toFixed(2)}` : `${deg(a).toFixed(0)}°, Lambertian`; }, 'The screen’s full angle at half brightness, 40° to 180°. 180° is Lambertian; narrower, the ghost dims where the sheet sees the screen from the side.'],
  ['stageLevel', PT.STAGE_LEVEL, (v) => `${signed(linear(V(v), kStageStopsMin, kStageStopsMax), 2)} stops`, 'The stage’s light behind the sheet, −4 to +2 stops. Dim the house and the ghost stands out.'],
  ['spill', PT.SPILL, (v) => `${(SpillFromParam(V(v)) * 100).toFixed(1)} %`, 'What the sheet reflects besides the screen — the pit, the room — up to 50 % of white.'],

  ['rig', PT.RIG, null, 'Pane: one sheet leaning over a floor screen, as on a stage. Pyramid: three or four sheets standing on a screen, as the phone toy is.'],
  ['mount', PT.MOUNT, null, 'Screen Below: the screen on the floor or the table, facing up. Screen Above: overhead, facing down; the pane hangs from it and the pyramid stands apex up under it.'],
  ['sheetAngle', PT.SHEET_ANGLE, (v) => `${deg(SheetAngleFromParam(V(v))).toFixed(1)}°`, 'The sheet’s angle to the screen, 25° to 65°. At 45° the pane’s ghost stands upright.'],
  ['sides', PT.SIDES, null, 'Pyramid: four faces or three.'],
  ['centre', PT.CENTRE, (v) => `${CentreFromParam(V(v)).toFixed(3)} of the short side`, 'Pyramid: the size of its small end, 0.1 to 0.6 of the screen’s short side.'],
  ['screen', PT.SCREEN, null, 'Pyramid: Feed lays the screen clip out as SW Pepper Feed would, one copy for each face; As Is takes the clip as an already laid-out feed.'],

  ['sheet', PT.SHEET, null, 'PET Foil (1.65 at the d line, Musion’s foil), Float Glass (1.52, the Victorian plate), Acrylic (1.49, most pyramids), Polycarbonate (1.585). Each disperses: red, green and blue see their own index.'],
  ['coating', PT.COATING, null, 'A thin layer on the sheet: None, Aluminium (a metal partial mirror) or Titanium Dioxide (a high-index layer that reflects more and absorbs nothing). Its two faces interfere.'],
  ['coatThickness', PT.COAT_THICKNESS, coatText, 'Aluminium 0 to 10 nm (5 nm by default); titanium dioxide 0 to 2 quarter waves of green (one by default).'],
  ['thickness', PT.THICKNESS, (v) => { const t = ThicknessFromParam(V(v)); return t > 0 ? `${(t * 100).toFixed(2)} % of the rig` : 'a film'; }, '0 to 4 % of the rig. 0 is a film, its two surfaces’ images in one place; thicker, the back surface’s images land beside the front’s — the double ghost of plate glass.'],
  ['tint', PT.TINT_R, null, 'The sheet’s colour face-on: what it transmits of each channel, crossing it once.'],
  ['tintGreen', PT.TINT_G, null, ''],
  ['tintBlue', PT.TINT_B, null, ''],
  ['ripple', PT.RIPPLE, (v) => `${deg(RippleFromParam(V(v))).toFixed(2)}°`, 'The foil’s steepest slope, 0° to 3°: a tensioned membrane ringing in its frame’s modes, so the ghost wobbles. Glass is rigid; set it to 0.'],
  ['rippleSpeed', PT.RIPPLE_SPEED, (v) => `${RippleHzFromParam(V(v)).toFixed(2)} Hz`, 'The frame’s fundamental, 0.05 to 4 Hz; the higher modes ring at their own frequencies above it.'],

  ['distance', PT.DISTANCE, (v) => `${DistanceFromParam(V(v)).toFixed(2)} rig sizes`, 'How far the camera is, 1 to 12 rig sizes: the ghost’s height for the pane, 0.6 of the top’s width for the pyramid. The camera is a 30° lens orbiting the rig.'],
  ['elevation', PT.ELEVATION, (v) => `${signed(deg(ElevationFromParam(V(v))), 1)}°`, '−20° to 60°. The stage is what the camera sees behind the glass, so the view moves the glass and the ghost, not the stage.'],
  ['azimuth', PT.AZIMUTH, (v) => `${signed(deg(AzimuthFromParam(V(v))), 1)}°`, '−180° to 180°, round the rig.'],
];

const FEED_SPEC = [
  ['sides', FT.SIDES, null, 'Four arms for a four-sided pyramid, three for a three-sided one.'],
  ['centre', FT.CENTRE, (v) => `${CentreFromParam(V(v)).toFixed(3)} of the short side`, 'The pyramid’s small end, 0.1 to 0.6 of the screen’s short side: the hole in the middle of the cross. The same mapping as SW Pepper’s Centre, bit for bit.'],
  ['mount', FT.MOUNT, null, 'Screen Below lays each arm out with its top outward, for a pyramid standing apex down on a screen. Screen Above turns each arm’s clip through 180° in place, top inward, for one standing apex up under a screen facing down.'],
];

function paramsFor(spec, table, defaults) {
  const types = { boolean: 'boolean', standard: 'standard', option: 'option', red: 'colour', green: 'colour', blue: 'colour' };
  return spec.map(([id, slot, display, hint]) => {
    const row = table.find((r) => r.slot === slot);
    const p = { id, slot, name: row.name, type: types[row.kind], default: defaults[slot], group: row.group, hint };
    if (row.elements) p.elements = row.elements;
    if (display) p.display = display;
    return p;
  });
}

//===========================================================================
// The pages.
//===========================================================================
const query = new URLSearchParams(window.location.search);
const stats = { text: '' };
const REPO = 'https://github.com/stoatworks-labs/pepper';
const PROJECT = 'https://stoatworks-labs.com/software/pepper/';

const SCREEN_CLIPS = ['spot', 'alpha', 'scene', 'grid', 'bars100', 'ramp', 'detail'];
const STAGE_CLIPS = ['grid', 'scene', 'bars', 'ramp', 'detail', 'spot'];
const STAGE_DEFAULT = 'grid';

const COMMON_GAPS = {
  port: (what, cases) => `The CPU half is a port. ${what} are translated to JavaScript by hand in demo/plugin.js. demo/tools/crosscheck/ rendered ${cases}; outside those, only a reader checks it. The shaders themselves are checked: demo/tools/check_shaders.py fails the repository’s verify script if a character drifts from source/Shaders.cpp.`,
  maxuv: 'MaxUV is 1. Resolume hands a plugin padded textures and the shader applies each input’s own MaxUV and half-texel clamp; the page’s textures are unpadded, so that part of the shader runs with nothing to correct.',
  es: 'GLSL ES 3.00 in WebGL2, not desktop GL 4.1 core. The shader text is the plugin’s; only the version line and precision qualifiers are changed by the kit. A different GPU and driver round differently in the last bit, which at a sheet’s edge can let one of a pixel’s four rays land on the other side of it.',
  presets: 'The presets are the page’s. The plugin declares none; each is only a combination of the plugin’s own parameters.',
  about: 'The About block — a text line and link buttons for the host’s panel — is absent: a web page has links of its own. So are the plugin’s log file and the harness’s negative controls (the Fault uniform is 0, as shipped). There is no audio path, so nothing here is missing for want of one.',
};

function mountPepper() {
  function createRenderer(gl, quad) {
    const program = programCache(gl);
    const instance = new PepperInstance(gl, quad, program('pepper'));
    const stage = new SourceRenderer(gl, quad);
    installHook(gl, quad, program);

    return {
      render({ input, params, width, height, time, variant }) {
        // The host restates every parameter each frame.
        for (const p of PEPPER_PARAMS) instance.SetFloatParameter(p.slot, params.get(p.id));

        // The stage, Dest: a second copy of the kit's clip generator at the
        // output's raster and on the same clock.
        const clip = SOURCES.find((x) => x.id === (variant ?? STAGE_DEFAULT)) ?? SOURCES[0];
        const dest = stage.render(clip, width, height, time);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, width, height);
        gl.disable(gl.BLEND);

        instance.ProcessOpenGL(dest, input, width, height, time);

        const s = instance.lastSetup;
        const parts = [];
        parts.push(`${SHEET_NAMES[OptionIndex(instance.params[PT.SHEET], SheetKind.Count)]} n ${s.eta.map((n) => n.toFixed(4)).join(' / ')} (R / G / B)`);
        parts.push(s.rig === RigKind.Pane ? `pane at ${deg(s.sheetAngle).toFixed(1)}°` : `${s.sides}-sided pyramid at ${deg(s.sheetAngle).toFixed(1)}°, ${s.map === ScreenMap.Feed || s.map === ScreenMap.FeedMirrored ? 'fed' : 'screen as is'}`);
        if (s.coating !== CoatingKind.None) parts.push(`${COATING_NAMES[s.coating]} coat ${s.coatNm.toFixed(2)} nm`);
        if (s.thickness > 0) parts.push(`${(s.thickness * 100 / s.rigSize).toFixed(2)} % thick`);
        parts.push(`screen ${s.sourceGain > 0 ? `${signed(stops(s.sourceGain), 2)} stops` : 'off'}, stage ${signed(stops(s.stageGain), 2)} stops`);
        const amp = Math.max(...s.face.slice(0, s.faces).flatMap((f) => f.amplitude.map(Math.abs)));
        parts.push(amp > 0 ? `ripple ${deg(RippleFromParam(instance.params[PT.RIPPLE])).toFixed(2)}° at ${RippleHzFromParam(instance.params[PT.RIPPLE_SPEED]).toFixed(2)} Hz, t ${time.toFixed(2)} s` : 'no ripple');
        stats.text = parts.join(' · ');
      },
    };
  }

  const PEPPER_PARAMS = paramsFor(PEPPER_SPEC, PEPPER_TABLE, PepperDefaults());

  const P = (id) => PEPPER_PARAMS.find((p) => p.id === id);
  const brewster = ParamForSheetAngle(0.5 * kPi - Math.atan(ChannelIndex(SheetKind.PetFoil, kRayChannel)));

  const mounted = mountDemo({
    name: 'SW Pepper',
    kind: 'mixer',
    pluginId: 'PG01',
    tagline:
      'Pepper’s ghost as an FFGL mixer: a clear sheet at an angle reflecting a hidden screen over the stage behind it, traced per pixel in linear light. The Victorian pane, a Musion-style foil and the plastic pyramid on a phone, with dispersion, coated foils, polarisation through every face, a thick pane’s double image and a foil that ripples.',
    repo: REPO,
    page: PROJECT,
    blurb:
      'It is SW Pepper’s own GLSL, ported from the repository to WebGL2 with the one expression WebGL refuses (a ?: on a struct) spelled as an equivalent call, fed by a JavaScript port of the plugin’s CPU half — its controls, its rig, its foil’s ripple and its coat’s table. SW Pepper is a mixer, so it needs two pictures: the Stage (Dest, the layer below) and the Screen (Src, this layer: the hidden screen whose reflection is the ghost), both generated in this page unless you use your own for the screen. In Resolume the layer’s opacity fader is the screen’s light; here it is the Opacity slider.',
    sources: SCREEN_CLIPS,
    variants: {
      label: 'Stage',
      default: STAGE_DEFAULT,
      options: STAGE_CLIPS.map((id) => {
        const x = SOURCES.find((y) => y.id === id);
        return { id, name: x.name, hint: `Dest, the layer below: the stage behind the sheet. ${x.hint}` };
      }),
    },
    params: PEPPER_PARAMS,
    // The plugin declares no presets. These are the page's: combinations of
    // the plugin's own parameters and nothing else.
    presets: {
      'Victorian plate glass, double ghost': { sheet: SheetKind.FloatGlass, thickness: ParamForThickness(0.025), ripple: 0 },
      'Aluminised foil': { coating: CoatingKind.Aluminium, coatThickness: 0.5 },
      'Titania-coated foil': { coating: CoatingKind.TitaniumDioxide, coatThickness: 0.5 },
      'Polarised screen at Brewster’s angle': { polarisation: Polarisation.Vertical, sheetAngle: brewster },
      'Slack foil, rippling': { ripple: ParamForRipple(1.2 * kDegree), rippleSpeed: ParamForRippleHz(1.0) },
      'Screen overhead': { mount: MountKind.Above },
      'Phone pyramid': { rig: RigKind.Pyramid, sheet: SheetKind.Acrylic, ripple: 0, elevation: ParamForElevation(10 * kDegree) },
      'Pyramid, apex up under a screen': { rig: RigKind.Pyramid, mount: MountKind.Above, sheet: SheetKind.Acrylic, ripple: 0, elevation: ParamForElevation(10 * kDegree) },
      'From the side': { azimuth: ParamForAzimuth(40 * kDegree), elevation: ParamForElevation(12 * kDegree), distance: ParamForDistance(3.2) },
      'Dark house': { stageLevel: ParamForStageStops(-2.5) },
      'Gamma-space add (Linear Light off)': { linearLight: 0 },
    },
    differences: [
      'Two inputs, both generated here. The kit this page is built on hands a demo one input; SW Pepper is a mixer, so the Stage (Dest) is rendered by a second copy of the kit’s own clip generator at the same raster and on the same clock, picked with the Stage dropdown. The Screen (Src) is the kit’s Clip picker, and “Use my own…” replaces it — your picture becomes the ghost. They are synthetic test clips, not footage.',
      'Opacity is a slider. In Resolume Arena a mixer parameter named Opacity is bound to the layer’s opacity fader, and a layer transition ramps it — measured on genlock, wipe, relay and lenticular, not on SW Pepper, which has never been loaded into Resolume.',
      'Linear Light is parameter 0. Arena hides a mixer’s first parameter, which is why it is first and on: in Arena it stays on. Here it is shown and can be turned off.',
      COMMON_GAPS.port('Controls.cpp (every conversion from the host’s 0..1 and every sheet’s index at each channel’s wavelength), Rig.cpp (the sheets, the screen, the camera and the foil’s ripple modes, their PCG hash in uint32), Coating.cpp (the coat’s table, in double, with libc++’s complex formulas) and the CPU half of Pepper.cpp’s ProcessOpenGL (every uniform)', 'the page and pgtest --pipe, the plugin’s own harness, from the same frames in the cases AGENTS.md lists'),
      'The clock is the page’s. The foil’s ripple runs on its seconds, which pause and step with the transport; the plugin takes Resolume’s SetTime and works out its unit by watching it against the wall clock (Timing.cpp), which is not ported. As in the plugin, each mode’s phase is reduced in double before only its displacement reaches the GPU.',
      COMMON_GAPS.maxuv,
      'The kit’s clips are premultiplied and so is a picture of your own once uploaded; the shader multiplies each input’s colour by its alpha, as the plugin does with whatever its host hands it. Where a screen clip is partly transparent (Shape on transparency) the page’s ghost is the plugin’s over premultiplied input. The stage clips are all opaque, so the output is too, as the plugin makes it wherever the stage is: a ghost over a transparent layer, which the plugin gives its own alpha, is not shown here.',
      'The trace is the plugin’s cost: four rays a pixel, each through every sheet it meets, up to 64 a pixel with a thick sheet. The page renders at 960 × 540 by default; on a slow GPU the frame rate drops, and the plugin would do the same at that size.',
      COMMON_GAPS.es,
      'One expression of SW Pepper’s shader is respelled, the only change past the kit’s. WebGL refuses the ?: operator on a struct, which desktop GL and plain GLSL ES 3.00 allow, and the plugin’s trace uses one: a thick sheet’s third back-surface image carries the rest of the series, so each image’s weight is k < 3 ? W : cOver( W, echo ). The page hands the same two operands to a helper it adds, which returns the one the ternary would pick; the unpicked one is computed and thrown away. demo/tools/check_shaders.py fails the repository’s verify script if that line of source/Shaders.cpp changes, and demo/tools/crosscheck/ compares a thick sheet’s picture with pgtest’s.',
      'The line under the picture is not the plugin. It reports what the port computed for this frame — the sheet’s index at each channel, the rig, the coat, the gains, the ripple and the clock. The plugin draws no such thing.',
      COMMON_GAPS.presets,
      COMMON_GAPS.about,
      'Nothing here is measured. The plugin’s numerical proof — a black screen adds exactly nothing, R + T = 1 to 3e-7, Brewster’s null where atan n puts it for each colour, a thick pane’s second image where Snell puts it, the coat against an independent thin-film model, the pyramid against a separate 3D trace, the ripple’s six mode frequencies by FFT — is tools/pgtest in the repository, and that harness, not this page, is the reason to believe the optics.',
    ],
    showBackdrop: false,
    createRenderer,
  });

  livePage = mounted?.params ?? null;
  mounted?.params?.addEventListener('change', (event) => {
    if (event.detail?.id === 'coating') {
      mounted.params.dispatchEvent(new CustomEvent('change', { detail: { id: 'coatThickness', value: mounted.params.get('coatThickness') } }));
    }
  });

  // The kit labels its clip picker "Clip". With two inputs that is ambiguous,
  // so it is renamed for what it feeds: the hidden screen, Src. `?stage=<id>`
  // picks the stage, the way the kit's `?clip=` picks the screen.
  for (const label of document.querySelectorAll('.transport__label')) {
    if (label.textContent === 'Clip') {
      label.textContent = 'Screen';
      label.title = 'Src, this layer: the hidden screen, whose reflection in the sheet is the ghost.';
    }
  }
  const wantedStage = query.get('stage');
  if (mounted?.state && wantedStage && STAGE_CLIPS.includes(wantedStage)) {
    mounted.state.variant = wantedStage;
    const select = [...document.querySelectorAll('.transport__label')]
      .find((l) => l.textContent === 'Stage')?.closest('.transport__field')?.querySelector('select');
    if (select) select.value = wantedStage;
  }
  linkSibling('feed/', 'SW Pepper Feed, the pyramid’s layout');
  return mounted;
}

function mountFeed() {
  function createRenderer(gl, quad) {
    const program = programCache(gl);
    const instance = new FeedInstance(gl, quad, program('feed'));
    installHook(gl, quad, program);
    return {
      render({ input, params, width, height }) {
        for (const p of FEED_PARAMS) instance.SetFloatParameter(p.slot, params.get(p.id));
        instance.ProcessOpenGL(input, width, height);
        const l = instance.last;
        stats.text = [
          `${l.sides} arms, each ${l.w.toFixed(3)} × ${l.d.toFixed(3)} of the short side`,
          `from ${(0.5 * l.centre).toFixed(3)} out`,
          l.above ? 'tops inward (apex up)' : 'tops outward (apex down)',
        ].join(' · ');
      },
    };
  }

  const FEED_PARAMS = paramsFor(FEED_SPEC, FEED_TABLE, FeedDefaults());

  const mounted = mountDemo({
    name: 'SW Pepper Feed',
    kind: 'effect',
    pluginId: 'PG02',
    tagline:
      'The cross layout a real pyramid hologram is fed, as an FFGL effect: one copy of the clip per face, each a mirror image that the face’s reflection undoes, around a hole the size of the pyramid’s small end. The same layout SW Pepper looks into under Rig = Pyramid and Screen = Feed.',
    repo: REPO,
    page: PROJECT,
    blurb:
      'It is SW Pepper Feed’s own GLSL, ported from the repository to WebGL2, with the plugin’s conversions and layout arithmetic ported to JavaScript, running on generated clips in this page — or a picture of your own. To see the layout through a simulated pyramid, open SW Pepper with Rig set to Pyramid.',
    sources: ['scene', 'grid', 'spot', 'alpha', 'bars', 'ramp'],
    params: FEED_PARAMS,
    presets: {
      'Three-sided pyramid': { sides: 1 },
      'Apex up, under a screen': { mount: MountKind.Above },
      'Small top': { centre: ParamForCentre(0.15) },
      'Wide top': { centre: ParamForCentre(0.5) },
    },
    differences: [
      COMMON_GAPS.port('Controls.cpp’s Sides and Centre, Rig.cpp’s FeedArm and ArmAxes and the CPU half of Feed.cpp’s ProcessOpenGL (every uniform)', 'the page and pgtest --plugin feed, the plugin’s own harness, from the same card in the cases AGENTS.md lists'),
      COMMON_GAPS.maxuv,
      'The output is opaque: black outside the arms, as the plugin makes it, because a real screen emits black. The kit’s clips are premultiplied, and the shader multiplies colour by alpha as the plugin does, so a partly transparent clip is shown over black twice-multiplied here.',
      COMMON_GAPS.es,
      'The line under the picture is not the plugin. It reports the arms the port computed. The plugin draws no such thing.',
      COMMON_GAPS.presets,
      COMMON_GAPS.about,
      'Nothing here is measured. The plugin’s checks — the arms are mirror images where stated, and the mixer’s pyramid agrees with this layout, below and above — are tools/pgtest in the repository.',
    ],
    showBackdrop: false,
    createRenderer,
  });
  linkSibling('../', 'SW Pepper, the mixer that looks into it');
  return mounted;
}

/** The kit's header links the repository; this adds the sibling plugin's page. */
function linkSibling(href, text) {
  const links = document.querySelector('.head__links');
  if (!links) return;
  const a = document.createElement('a');
  a.className = 'head__link';
  a.href = href;
  a.textContent = text;
  links.append(a);
}

const mounted = PAGE_PLUGIN === 'feed' ? mountFeed() : mountPepper();

// The line under the picture.
const statLine = document.createElement('p');
statLine.className = 'stage__status';
statLine.setAttribute('aria-live', 'off');
statLine.dataset.role = 'pepper-stats';
document.querySelector('.stage')?.append(statLine);
if (statLine.isConnected) {
  const tick = () => {
    if (statLine.textContent !== stats.text) statLine.textContent = stats.text;
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
}

// For a headless check: the mounted page.
window.pepperPage = { mounted, plugin: PAGE_PLUGIN, stats };
