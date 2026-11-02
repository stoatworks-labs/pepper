// The page's half of the crosscheck: fetch the cases and their frames from
// crosscheck.py's server, render each case through window.pepperDemo.renderCase,
// and post the picture back, with the page's tables of controls and any
// uniform the page set that a program lacks or a program has that nothing set.
// crosscheck.py compares; nothing is judged here.
const post = (path, body) => fetch(path, { method: 'POST', body });

async function main() {
  for (let i = 0; i < 400 && !window.pepperDemo; i += 1) await new Promise((r) => setTimeout(r, 50));
  if (!window.pepperDemo) throw new Error('window.pepperDemo never appeared: the page did not start');
  const job = await (await fetch('/xc/job.json')).json();
  const frames = {};
  const frame = async (key) => {
    if (key == null) return null;
    if (!frames[key]) frames[key] = new Uint8Array(await (await fetch(`/xc/frame/${key}`)).arrayBuffer());
    return frames[key];
  };
  await post('/xc/table', JSON.stringify(window.pepperDemo.table));
  const missing = new Set();
  const unset = new Set();
  const errors = [];
  for (const c of job.cases) {
    const out = window.pepperDemo.renderCase({
      plugin: c.plugin,
      settings: c.settings,
      width: c.width,
      height: c.height,
      dest: await frame(c.dest),
      src: await frame(c.src),
      srcWidth: c.srcWidth,
      srcHeight: c.srcHeight,
      // pgtest --pipe shows frame k at k / fps: the case's frame is the second.
      seconds: c.fps ? 1 / Number(c.fps) : 0,
    });
    out.missing.forEach((name) => missing.add(`${c.plugin}: ${name}`));
    out.unset.forEach((name) => unset.add(`${c.plugin}: ${name}`));
    if (out.error) errors.push(`${c.name}: GL error ${out.error}`);
    await post(`/xc/out/${c.name}`, out.pixels);
  }
  await post('/xc/missing', JSON.stringify({ missing: [...missing], unset: [...unset], errors }));
  await post('/xc/done', 'ok');
}

main().catch((error) => post('/xc/error', String(error?.stack ?? error)));
