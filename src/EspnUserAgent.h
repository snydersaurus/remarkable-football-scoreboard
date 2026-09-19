#pragma once

/*
 * The User-Agent every ESPN request has to carry.
 *
 * ESPN sits behind Akamai, and the edge answers 403 Access Denied to a
 * User-Agent it does not recognise -- including no header at all, "Wget/1.21",
 * "Qt/6.8" and the app's own "rmpp-scoreboard/1.0", which is what the baseball
 * app sends and what MLB is perfectly happy with. Verified by hand against
 * site.api.espn.com: the decision is made on the leading token, is completely
 * deterministic, and a browser-shaped string is refused as well, so imitating
 * Chrome is no help either. A string beginning with a known client token
 * ("curl/", "python-requests/", "okhttp/") is let through.
 *
 * So the header leads with curl's token, which is what actually opens the
 * gate, and then says who is really calling. Getting this wrong is not subtle
 * and not slow: every request fails and the footer reads OFFLINE forever.
 *
 * The logo CDN (a.espncdn.com) does not care either way; it is sent the same
 * header for the sake of having one answer to the question.
 */
namespace Espn {
constexpr const char *kUserAgent = "curl/8.7.1 rmpp-scoreboard/1.0";
}
