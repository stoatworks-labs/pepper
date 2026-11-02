/*
 * Stoatworks Labs - About window data for Pepper.
 *
 * A PROVISIONAL hand copy, adapted from lenticular's generated header on
 * 2026-11-02, as graticule's was before its first sync. stoatworks-backend's
 * scripts/sync-about.py generates this file from the website's projects.json
 * once Pepper is registered there; until then it is written by hand and the
 * next sync overwrites it. `guide` is empty because no user guide exists yet,
 * and an empty link is left out of the About block (StoatworksAboutLinks.h).
 *
 * `version` here is a fallback read from this repo's own manifest at sync
 * time. Anything with a build step injects the real one at build time and
 * overrides this.
 */
#pragma once

namespace stoatworks::about
{
    inline constexpr auto name = "Pepper";
    inline constexpr auto slug = "pepper";
    inline constexpr auto hook = "Pepper's ghost, a pane or a pyramid, for Resolume";
    inline constexpr auto licence = "MIT";
    inline constexpr auto guide = "";
    inline constexpr auto page = "https://stoatworks-labs.com/software/pepper/";
    inline constexpr auto repo = "https://github.com/stoatworks-labs/pepper";
    inline constexpr auto versionFallback = "v0.1.0";

    inline constexpr auto org = "Stoatworks Labs";
    inline constexpr auto home = "https://stoatworks-labs.com";
    inline constexpr auto tagline = "Open tools for the people who run the show.";

    /* The canonical funding links, matching FUNDING.yml and the support footer. */
    struct Link { const char* name; const char* url; };
    inline constexpr Link funding[] = {
        { "GitHub Sponsors", "https://github.com/sponsors/stoatworks-labs" },
        { "Ko-fi", "https://ko-fi.com/stoatworkslabs" },
        { "Patreon", "https://patreon.com/StoatworksLabs" },
        { "Liberapay", "https://liberapay.com/stoatworks-labs" },
    };
}
