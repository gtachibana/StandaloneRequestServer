<?php

// Shown in the page header, unless OpenKJ's own event settings supply a name.
$venueName = 'Sing73k';

// The timezone this venue keeps, as a PHP timezone identifier
// (https://www.php.net/manual/en/timezones.php). OpenKJ reports timestamps in
// its own local time with no UTC offset attached - the singer history's
// last-sung date is currently the only one - so PHP has to be told which zone
// that is to read them back. It cannot be guessed: PHP falls back to UTC
// whenever php.ini leaves date.timezone unset, which is the case in the stock
// php:8.0-apache image, and it ignores the TZ environment variable, so a venue
// west of Greenwich reads dates a day out until this is right. Leave it empty
// to keep whatever the PHP install is already configured with.
$venueTimezone = 'Pacific/Honolulu';

// Base URL of the OpenKJ Local Mode embedded API. OpenKJ serves this when
// Settings > app mode is Local Mode and the embedded API is enabled; it
// defaults to port 5050 bound to 0.0.0.0.
$openkjApiBase = 'http://127.0.0.1:5050';

// Seconds to wait on the API before giving up. The embedded API runs on
// OpenKJ's Qt main thread, so keep this short - a slow request there is a
// stalled UI for the KJ.
$openkjTimeout = 4;

// Addresses of proxies in front of *this app* whose client-address headers
// (CF-Connecting-IP, then X-Forwarded-For) may be believed. Loopback is always
// trusted, which already covers a tunnel connector or reverse proxy running on
// this machine, so this is only needed when that proxy reaches the webserver
// over the network.
//
// It exists because OpenKJ's rate limits - the cheer bucket and the login
// lockout - key on whoever called the API, and that is always this webserver:
// without a forwarded address the whole room shares one bucket, so an ovation
// throttles itself and one singer's fumbled password locks everybody out. Empty
// is the safe default: a header believed from an untrusted hop is a header the
// phone chose, and a phone that picks its own address can sidestep the lockout
// at will. The API in turn only reads what we send when *it* trusts this
// webserver - loopback, or its own embeddedApiTrustedProxies list.
$trustedProxies = array();

// Max songs to pull back per search (the API itself clamps to 500) and per
// browse-by-letter page (clamped to 1000).
$searchLimit = 200;
$browseLimit = 300;

// Max songs to pull back for a singer's own history (the API clamps to 500).
// One row per distinct song they've sung, so this goes a long way.
$historyLimit = 100;

// Base URL of the same embedded API *as the phone sees it*. The queue view
// subscribes to GET /local/events (Server-Sent Events) here to learn about
// rotation changes instead of polling for them, so this has to be a host the
// browser can reach directly - $openkjApiBase above is resolved server-side and
// is often 127.0.0.1 or a Docker-internal name, neither of which works here.
//
// "/" means same-origin: the stream is fetched from whatever host served the
// page, so it rides the tunnel the rest of the site already goes through and
// port 5050 never has to be published on its own. That requires a proxy rule
// routing /local/events to the API - see "Exposing the stream" in the README,
// and set this back to '' until that rule is live, or the stream 404s and the
// rotation is left on the slow $queueStreamPollSeconds fallback.
//
// Naming the API host outright (e.g. http://192.168.1.20:5050) also works, but
// only do that on a LAN nothing is tunneled out of. The embedded API is built
// to sit behind this app: current OpenKJ builds gate the admin routes behind a
// session token, but everything else on it is unauthenticated by design, and
// builds predating that gate let anyone who can reach the port drive the
// rotation outright.
//
// Leave empty to disable the stream and poll only.
$openkjEventStreamBase = '/';

// --- YouTube requests -------------------------------------------------
//
// These only matter when OpenKJ has YouTube requests switched on *and* its
// yt-dlp is working: the tab is gated on /local/capabilities'
// supportsYoutubeRequests, which reports both. Nothing below turns the feature
// on - that switch is OpenKJ's, in Settings > YouTube.
//
// OpenKJ never searches YouTube itself; it only accepts a video a singer has
// already picked. So this app does the searching, in one of two ways:
//
// Empty key (the default) reads the ordinary youtube.com results page. No key,
// no quota, and no signup - but it is scraping, so a YouTube redesign can break
// it (the parsing is all in youtube-search.inc). Setting a Google Cloud
// YouTube Data API v3 key here switches to the official API instead. That is
// the sturdier route, at the cost of a quota that is tighter than it sounds:
// each search costs 100 of 10,000 units a day, so roughly a hundred searches
// for the entire venue per day, shared by every phone in the room.
$youtubeApiKey = '';

// Results per search. Kept short deliberately - this is a phone screen, and the
// karaoke track a singer wants is almost always in the first handful.
$youtubeSearchLimit = 15;

// Seconds to wait on YouTube. Longer than $openkjTimeout on purpose: that one
// is short because a slow call stalls the KJ's UI, and this one reaches the
// public internet and stalls nothing but the singer's own page.
$youtubeSearchTimeout = 8;

// Silently appended to whatever the singer typed, unless their query already
// contains it. Without this, searching "purple rain" returns the studio
// recording - which is requestable, and useless to sing over. Set to '' to
// search for exactly what they typed.
$youtubeSearchSuffix = 'karaoke';

// How often, in seconds, phones re-poll the queue view. The stream carries
// updates when it is configured, so the poll is only a safety net there - which
// is why it is much slower. Older OpenKJ builds have no /local/events and the
// stream shuts itself down on the 404, leaving the fallback poll in charge.
$queuePollSeconds = 10;
$queueStreamPollSeconds = 60;

?>
