// Preferences for the Firefox checks (copied into a fresh profile by checks/firefox-window.json).
// Every one is a documented Firefox preference; none changes what a page is given.
//
// Rendering: this machine has no GPU driver, so WebRender's software renderer is asked for
// outright instead of being reached by fallback (the fallback first tries OpenGL, which here is
// Mesa's software rasteriser and far slower), and compositing stays in the main process (the
// GPU process is a second step, tried on its own).
user_pref("gfx.webrender.software", true);
user_pref("layers.gpu-process.enabled", false);
// First-run pages, default-browser and telemetry prompts: the checks want the page they asked for.
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.aboutwelcome.enabled", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("startup.homepage_welcome_url", "");
user_pref("startup.homepage_welcome_url.additional", "");
user_pref("datareporting.policy.dataSubmissionEnabled", false);
user_pref("toolkit.telemetry.reportingpolicy.firstRun", false);
user_pref("app.update.auto", false);
// Emulation is slow: one content process, none started ahead of need.
user_pref("dom.ipc.processCount", 1);
user_pref("dom.ipc.processPrelaunch.enabled", false);
// No background traffic: a check fetches the page it names and nothing else (this is the owner's
// network; these are the switches Mozilla's own test harnesses set).
user_pref("network.captive-portal-service.enabled", false);
user_pref("network.connectivity-service.enabled", false);
user_pref("browser.safebrowsing.malware.enabled", false);
user_pref("browser.safebrowsing.phishing.enabled", false);
user_pref("browser.safebrowsing.downloads.remote.enabled", false);
user_pref("browser.safebrowsing.provider.mozilla.updateURL", "");
user_pref("browser.safebrowsing.provider.google4.updateURL", "");
user_pref("services.settings.server", "data:,#remote-settings-dummy/v1");
user_pref("app.normandy.enabled", false);
user_pref("extensions.update.enabled", false);
user_pref("extensions.getAddons.cache.enabled", false);
user_pref("extensions.systemAddon.update.enabled", false);
user_pref("media.gmp-manager.url", "");
user_pref("toolkit.telemetry.enabled", false);
user_pref("toolkit.telemetry.unified", false);
user_pref("datareporting.healthreport.uploadEnabled", false);
user_pref("browser.search.suggest.enabled", false);
user_pref("browser.search.update", false);
user_pref("browser.region.network.url", "");
user_pref("browser.region.update.enabled", false);
user_pref("network.prefetch-next", false);
user_pref("network.dns.disablePrefetch", true);
user_pref("network.predictor.enabled", false);
user_pref("browser.newtabpage.activity-stream.feeds.system.topstories", false);
user_pref("browser.newtabpage.activity-stream.feeds.telemetry", false);
// The first-run "Welcome to Firefox" sheet (Terms of Use, data choices) covers the page until
// someone answers it. A check neither accepts terms on anyone's behalf nor clicks through: the
// sheet is switched off the way Mozilla's own test harnesses do. Whoever uses this Firefox for
// real starts it without this file and answers the sheet himself.
user_pref("browser.preonboarding.enabled", false);
