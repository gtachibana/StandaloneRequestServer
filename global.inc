<?php
include_once("settings.inc");

// OpenKJ hands over local wall-clock times with no UTC offset, so PHP has to be
// standing in the venue's zone to read one back correctly - and PHP defaults to
// UTC whenever php.ini says nothing, which the stock php:8.0-apache image does.
// An identifier PHP doesn't know is left alone rather than allowed to warn on
// every page; DateTimeZone is what validates it, since it accepts the legacy
// names (US/Hawaii and friends) that timezone_identifiers_list() omits.
if (isset($venueTimezone) && $venueTimezone !== '')
{
  try { date_default_timezone_set((new DateTimeZone($venueTimezone))->getName()); }
  catch (Exception $e) { /* keep the PHP install's own setting */ }
}

// Session holds the OpenKJ user session token. Must be started before any
// output, which is also why every header() call has to precede siteheader().
if (session_status() === PHP_SESSION_NONE) session_start();

// The token also gets a cookie of its own, because the session cannot keep it.
// PHP's session cookie has no expiry by default, so it dies when the phone's
// browser does, and the server-side data behind it is reaped after
// session.gc_maxlifetime - 24 minutes on a stock install, and that is a php.ini
// setting that doesn't travel with this repo. Meanwhile the OpenKJ token is good
// for 30 days and slides forward every time it is used, so the credential outlives
// its container by a wide margin. Carrying the token separately also keeps the
// rest of the session on its own short life: the favorites id set and the
// capabilities cache want their 300s TTLs, not a year.
define('OKJ_TOKEN_COOKIE', 'okj_token');
// Chrome and Firefox clamp any cookie expiry past 400 days, so this is the real
// ceiling whatever number is written here. It doesn't need to be bigger: the
// cookie is re-issued on every request, so the clock restarts at each visit and
// only an absence longer than the cap can run it out.
define('OKJ_TOKEN_COOKIE_DAYS', 400);

okj_token_cookie_sync();


/* ------------------------------------------------------------------ *
 * OpenKJ Local Mode API client
 * ------------------------------------------------------------------ */

// Normalized shape for every call: ok, status, data, error.
function okj_failure($message, $status = 0)
{
  return array('ok' => false, 'status' => $status, 'data' => array(), 'error' => $message);
}

// The API reports logical failures two different ways depending on the route
// family: /local/* returns {"ok":false,"error":"..."} while the legacy
// /api.php commands return {"error":"true","errorString":"..."}.
function okj_interpret($data, $status)
{
  if (array_key_exists('ok', $data))
  {
    $ok = (bool)$data['ok'];
    $error = $ok ? '' : (isset($data['error']) ? $data['error'] : 'Request failed');
  }
  elseif (array_key_exists('error', $data) && is_string($data['error']))
  {
    $ok = ($data['error'] === 'false');
    $error = $ok ? '' : (isset($data['errorString']) ? $data['errorString'] : 'Request failed');
  }
  else
  {
    $ok = ($status >= 200 && $status < 300);
    $error = $ok ? '' : 'Request failed';
  }
  if ($status < 200 || $status >= 300)
  {
    $ok = false;
    if ($error === '') $error = "KJ software returned HTTP $status";
  }
  return array('ok' => $ok, 'status' => $status, 'data' => $data, 'error' => $error);
}

// Whether a client-address header arriving here may be believed - i.e. whether
// the thing that made this request is a proxy we put there ourselves. Mirrors
// isTrustedProxy() on the OpenKJ side, including trusting loopback outright: a
// tunnel connector or reverse proxy sharing this machine reaches PHP over
// 127.0.0.1, and nothing off-box can forge that.
function okj_proxy_is_trusted($peer)
{
  global $trustedProxies;

  if ($peer === '') return false;
  if ($peer === '127.0.0.1' || $peer === '::1' || $peer === '::ffff:127.0.0.1') return true;

  $trusted = (isset($trustedProxies) && is_array($trustedProxies)) ? $trustedProxies : array();
  foreach ($trusted as $entry)
  {
    if (strcasecmp(trim($entry), $peer) === 0) return true;
  }
  return false;
}

// The address OpenKJ should bucket this request under. It matters because every
// call this app makes is server-side: left to itself the API sees one client -
// this webserver - for the entire room, so the whole venue shares one cheer
// bucket (an ovation throttling itself) and one login lockout (one person's
// fumbled password shutting every singer, and the KJ, out for five minutes).
//
// Believed only from a trusted hop, and for the same reason OpenKJ refuses to
// believe it from an untrusted one: the value keys a lockout, so anyone able to
// choose it freely can dodge the lockout entirely by picking a new address per
// attempt. Unset $trustedProxies is the safe reading - the room shares a bucket
// the way it always did, which is a nuisance rather than a hole.
function okj_client_address()
{
  static $address = null;
  if ($address !== null) return $address;

  $peer = isset($_SERVER['REMOTE_ADDR']) ? $_SERVER['REMOTE_ADDR'] : '';
  $address = $peer;
  if (!okj_proxy_is_trusted($peer)) return $address;

  // Cloudflare's header first, as OpenKJ does: the edge sets it and discards any
  // copy the phone sent, so it can't be spoofed from outside. X-Forwarded-For is
  // the general fallback, leftmost entry being the original client.
  $forwarded = isset($_SERVER['HTTP_CF_CONNECTING_IP']) ? $_SERVER['HTTP_CF_CONNECTING_IP'] : '';
  if ($forwarded === '' && isset($_SERVER['HTTP_X_FORWARDED_FOR']))
  {
    $parts = explode(',', $_SERVER['HTTP_X_FORWARDED_FOR']);
    $forwarded = $parts[0];
  }
  $forwarded = trim($forwarded);

  // Validated rather than passed on as written: on the API side this becomes a
  // hash key held until the idle sweep, and a header is a header.
  if ($forwarded !== '' && filter_var($forwarded, FILTER_VALIDATE_IP)) $address = $forwarded;
  return $address;
}

function okj_call($method, $path, $body = null, $query = array())
{
  global $openkjApiBase, $openkjTimeout;

  $url = rtrim($openkjApiBase, '/') . $path;
  // RFC3986, not http_build_query's RFC1738 default: the latter encodes a space
  // as '+', and OpenKJ parses the query string with Qt, which only
  // percent-decodes and leaves '+' as a literal plus. That silently turns every
  // multi-word search into a search for "creep+radio" and no results.
  if ($query) $url .= '?' . http_build_query($query, '', '&', PHP_QUERY_RFC3986);
  $payload = ($method === 'POST') ? json_encode($body === null ? new stdClass() : $body) : null;

  // Who this call is really on behalf of. OpenKJ only reads it when this
  // webserver is a hop it trusts, and falls back to our address when it isn't -
  // which is exactly the behaviour we had before the header existed.
  $headers = array('Content-Type: application/json', 'Accept: application/json');
  $client = okj_client_address();
  if ($client !== '') $headers[] = 'X-Forwarded-For: ' . $client;

  if (function_exists('curl_init'))
  {
    $ch = curl_init($url);
    curl_setopt_array($ch, array(
      CURLOPT_RETURNTRANSFER => true,
      CURLOPT_CONNECTTIMEOUT => 2,
      CURLOPT_TIMEOUT => $openkjTimeout,
      CURLOPT_HTTPHEADER => $headers,
    ));
    if ($method === 'POST')
    {
      curl_setopt($ch, CURLOPT_POST, true);
      curl_setopt($ch, CURLOPT_POSTFIELDS, $payload);
    }
    $raw = curl_exec($ch);
    $status = (int)curl_getinfo($ch, CURLINFO_RESPONSE_CODE);
    $curlError = curl_error($ch);
    curl_close($ch);
    if ($raw === false) return okj_failure($curlError ? $curlError : 'Could not reach the KJ software');
  }
  else
  {
    $options = array('http' => array(
      'method' => $method,
      'header' => implode("\r\n", $headers) . "\r\n",
      'timeout' => $openkjTimeout,
      'ignore_errors' => true,
    ));
    if ($method === 'POST') $options['http']['content'] = $payload;
    $raw = @file_get_contents($url, false, stream_context_create($options));
    if ($raw === false) return okj_failure('Could not reach the KJ software');
    $status = 0;
    if (isset($http_response_header[0]) &&
        preg_match('!HTTP/\S+\s+(\d+)!', $http_response_header[0], $m)) $status = (int)$m[1];
  }

  $data = json_decode($raw, true);
  if (!is_array($data)) return okj_failure('Unexpected response from the KJ software', $status);
  return okj_interpret($data, $status);
}

function okj_get($path, $query = array())
{
  return okj_call('GET', $path, null, $query);
}

function okj_post($path, $body = array())
{
  return okj_call('POST', $path, $body);
}


/* ------------------------------------------------------------------ *
 * Session / current user
 * ------------------------------------------------------------------ */

function okj_token()
{
  return isset($_SESSION['okj_token']) ? $_SESSION['okj_token'] : '';
}

// Shared by every write below, so the flags can't drift apart between them - a
// cookie deleted with different attributes than it was set with is not deleted.
// Secure only when the request really is HTTPS: setting it on a plain-http venue
// would make the browser drop the cookie and nobody would stay signed in. The
// proxy's word on that is taken only from a proxy we already trust, the same test
// okj_client_address() applies to the address headers.
function okj_token_cookie_options($expires)
{
  $https = (!empty($_SERVER['HTTPS']) && strtolower($_SERVER['HTTPS']) !== 'off');
  if (!$https
    && isset($_SERVER['HTTP_X_FORWARDED_PROTO'])
    && okj_proxy_is_trusted(isset($_SERVER['REMOTE_ADDR']) ? $_SERVER['REMOTE_ADDR'] : ''))
  {
    $https = (strtolower(trim($_SERVER['HTTP_X_FORWARDED_PROTO'])) === 'https');
  }

  return array(
    'expires' => $expires,
    'path' => '/',
    'secure' => $https,
    // Nothing in this app reads the token from JS, and the event stream carries no
    // credential at all, so there is no reason for the page to be able to see it.
    'httponly' => true,
    // Lax rather than Strict: a singer following the venue's QR code or a link from
    // a group chat arrives cross-site, and Strict would show them signed out.
    'samesite' => 'Lax',
  );
}

function okj_remember_token($token)
{
  if (headers_sent()) return;
  setcookie(OKJ_TOKEN_COOKIE, $token, okj_token_cookie_options(time() + (OKJ_TOKEN_COOKIE_DAYS * 86400)));
}

// Drops the token everywhere it lives. The headers_sent() case is real: okj_user()
// discovers a rejected token in the middle of a render, long after the point where
// a cookie could still be written. It leaves a note instead, which the next
// request's okj_token_cookie_sync() acts on - otherwise the dead token would be
// rehydrated from the cookie and re-validated against the API forever.
function okj_forget_token()
{
  unset($_SESSION['okj_token']);
  if (headers_sent())
  {
    $_SESSION['okj_token_stale'] = true;
    return;
  }
  unset($_SESSION['okj_token_stale'], $_COOKIE[OKJ_TOKEN_COOKIE]);
  setcookie(OKJ_TOKEN_COOKIE, '', okj_token_cookie_options(time() - 3600));
}

function okj_store_token($token)
{
  $_SESSION['okj_token'] = $token;
  unset($_SESSION['okj_token_stale']);
  okj_remember_token($token);
}

// Runs on include, before any page has emitted a byte, because all three branches
// may need to write a cookie.
function okj_token_cookie_sync()
{
  if (!empty($_SESSION['okj_token_stale']))
  {
    okj_forget_token();
    return;
  }

  $token = okj_token();
  if ($token === '' && isset($_COOKIE[OKJ_TOKEN_COOKIE]) && $_COOKIE[OKJ_TOKEN_COOKIE] !== '')
  {
    $token = $_COOKIE[OKJ_TOKEN_COOKIE];
    $_SESSION['okj_token'] = $token;
  }

  // Rolling refresh: every visit pushes the expiry back out to the full window, so
  // a singer who turns up monthly restarts the clock ten months before the browser's
  // cap could ever matter. Costs one Set-Cookie header and no API call.
  if ($token !== '') okj_remember_token($token);
}

// Cached for the life of the request: every page that renders nav needs it.
function okj_user($refresh = false)
{
  static $user = null;
  if ($refresh) $user = null;
  if ($user !== null) return $user;

  $user = array('authenticated' => false, 'username' => '', 'inRotation' => false, 'away' => false);
  $token = okj_token();
  if ($token === '') return $user;

  $res = okj_get('/local/user/me', array('token' => $token));
  // An unreachable API has said nothing about this token, so it keeps it. The
  // distinction didn't matter while the token lived in a browser-session cookie,
  // but it does now: discarding it here would mean an OpenKJ restart mid-show
  // permanently signed out every phone in the room, each of them needing a
  // password nobody has typed since they set it. The singer sees signed-out for
  // as long as the outage lasts and is back afterwards without doing anything.
  if ($res['status'] === 0) return $user;

  if (!$res['ok'] || empty($res['data']['authenticated']))
  {
    // Token expired or was revoked on the OpenKJ side - the latter is now a real
    // path, since a KJ resetting a forgotten password ends that account's sessions.
    okj_forget_token();
    return $user;
  }

  $user = array(
    'authenticated' => true,
    'username' => isset($res['data']['username']) ? $res['data']['username'] : '',
    'inRotation' => !empty($res['data']['inRotation']),
    'away' => !empty($res['data']['away']),
  );
  return $user;
}

// Call after mutating anything /local/user/me reports (currently the away flag).
function okj_user_refresh()
{
  return okj_user(true);
}

// Builds the id set out of a /local/user/favorites payload and caches it. Kept
// separate so the favorites page, which has to fetch the full list anyway, can
// refresh the cache off a response it already paid for.
function okj_favorites_store($data, $userKey)
{
  $ids = array();
  $songs = (isset($data['songs']) && is_array($data['songs'])) ? $data['songs'] : array();
  foreach ($songs as $song)
  {
    // Keyed by id rather than listed, so the per-result lookup is an isset().
    if (isset($song['song_id'])) $ids[(int)$song['song_id']] = true;
  }
  $_SESSION['okj_favs'] = $ids;
  $_SESSION['okj_favs_user'] = $userKey;
  $_SESSION['okj_favs_until'] = time() + 300;
  return $ids;
}

// Which songs the signed-in singer has starred. Every search result consults
// this, so a round trip per render would put the cost of favorites on the
// keystroke path - hence the session cache. It is invalidated precisely, by
// okj_favorites_note() on our own edits; the TTL is only a backstop for a
// change made from the singer's other device.
function okj_favorite_ids($refresh = false)
{
  static $ids = null;
  if ($refresh) $ids = null;
  if ($ids !== null) return $ids;

  $user = okj_user();
  if (!$user['authenticated'])
  {
    $ids = array();
    return $ids;
  }

  // Keyed by user so a second sign-in on a shared phone can't inherit the
  // previous singer's stars.
  $userKey = strtolower($user['username']);
  if (!$refresh
      && isset($_SESSION['okj_favs'], $_SESSION['okj_favs_user'], $_SESSION['okj_favs_until'])
      && $_SESSION['okj_favs_user'] === $userKey
      && $_SESSION['okj_favs_until'] > time())
  {
    $ids = $_SESSION['okj_favs'];
    return $ids;
  }

  $res = okj_get('/local/user/favorites', array('token' => okj_token()));
  // A failed lookup renders every star empty for this request, but is
  // deliberately not cached - the next render tries again.
  if (!$res['ok'])
  {
    $ids = array();
    return $ids;
  }

  $ids = okj_favorites_store($res['data'], $userKey);
  return $ids;
}

// Keeps the cache in step with an edit we just made ourselves, instead of
// spending a re-fetch to learn something we already know.
function okj_favorites_note($songId, $isFavorite)
{
  if (!isset($_SESSION['okj_favs'])) return;
  $songId = (int)$songId;
  if ($isFavorite) $_SESSION['okj_favs'][$songId] = true;
  else unset($_SESSION['okj_favs'][$songId]);
}

// Search fires on nearly every keystroke and each render needs this, so it gets
// a few seconds of slack rather than a round trip every time. Going stale for
// that long is harmless: the request itself is still authoritative, and OpenKJ
// rejects one made against a closed queue.
function okj_accepting()
{
  static $accepting = null;
  if ($accepting !== null) return $accepting;

  if (isset($_SESSION['okj_accepting_until']) && $_SESSION['okj_accepting_until'] > time())
  {
    $accepting = (bool)$_SESSION['okj_accepting'];
    return $accepting;
  }

  $res = okj_post('/api.php', array('command' => 'venueAccepting'));
  $accepting = ($res['ok'] && !empty($res['data']['accepting']));
  if ($res['ok'])
  {
    $_SESSION['okj_accepting'] = $accepting;
    $_SESSION['okj_accepting_until'] = time() + 5;
  }
  return $accepting;
}

// What this OpenKJ build can do. Nothing needed this until cheers: every other
// optional feature announces itself by the presence of a field in a response
// this app already fetches, so array_key_exists() on the payload was enough.
// Cheers are the exception - now_playing.cheers is there whether or not the KJ
// has them switched on, and /local/capabilities is the only place that says.
//
// Session-cached like okj_event_name(), and for the same reason: this reads
// settings and touches no SQL, so it is about the cheapest call on the API, but
// it is still a round trip onto OpenKJ's Qt main thread and none of what it
// reports changes more than once a night. A KJ who flips a switch mid-show is
// caught by okj_capabilities_forget() below rather than by a short TTL.
function okj_capabilities()
{
  static $caps = null;
  if ($caps !== null) return $caps;

  if (isset($_SESSION['okj_caps'], $_SESSION['okj_caps_until'])
      && $_SESSION['okj_caps_until'] > time())
  {
    $caps = $_SESSION['okj_caps'];
    return $caps;
  }

  $res = okj_get('/local/capabilities');
  // Deliberately not cached on failure: an unreachable API would otherwise
  // leave the phone believing every optional feature is off for five minutes
  // after the KJ's laptop comes back.
  if (!$res['ok'])
  {
    $caps = array();
    return $caps;
  }

  $caps = $res['data'];
  $_SESSION['okj_caps'] = $caps;
  $_SESSION['okj_caps_until'] = time() + 300;
  return $caps;
}

// Precise invalidation for the one case the TTL handles badly: a control we
// offered came back refused because the KJ turned that feature off since we
// last looked. Same idea as okj_favorites_note() - fix the cache off a response
// already paid for instead of polling for the change.
function okj_capabilities_forget()
{
  unset($_SESSION['okj_caps'], $_SESSION['okj_caps_until']);
}

// The emoji OpenKJ floats up the singer screen for each reaction, mirrored from
// CheerWidget::glyphFor() in dlgcdg.cpp so a tap on the phone and the glyph on
// the wall are recognisably the same thing. Written as entities rather than
// literal UTF-8 so the source file's encoding can't quietly mangle them.
//
// This map is also the fallback ordering, for an OpenKJ that supports cheers
// but doesn't list them in /local/capabilities.
function okj_cheer_glyphs()
{
  return array(
    'clap'  => array('&#x1F44F;', 'Applause'),
    'fire'  => array('&#x1F525;', 'On fire'),
    'heart' => array('&#x1F496;', 'Love it'),
    'star'  => array('&#x2B50;',  'Star'),
    'party' => array('&#x1F389;', 'Party'),
  );
}

function okj_cheers_enabled()
{
  $caps = okj_capabilities();
  return !empty($caps['supportsCheers']);
}

// Whether to offer the YouTube tab at all. The capability is two conditions on
// OpenKJ's side, not one: the KJ's setting *and* a yt-dlp that actually runs.
// That is the whole reason to consult it rather than a switch in settings.inc -
// a missing or broken yt-dlp takes the tab off the phones instead of leaving a
// search box whose every result fails to queue.
function okj_youtube_enabled()
{
  $caps = okj_capabilities();
  return !empty($caps['supportsYoutubeRequests']);
}

// The cap OpenKJ will enforce on the request anyway. Fetched so a video over it
// can be marked before the singer taps it, rather than after a round trip that
// was always going to be refused. 0 means this build didn't say, in which case
// nothing is pre-filtered and OpenKJ remains the only judge.
function okj_youtube_max_duration()
{
  $caps = okj_capabilities();
  return isset($caps['youtubeMaxDurationSecs']) ? (int)$caps['youtubeMaxDurationSecs'] : 0;
}

// Which of these videos OpenKJ already has on disk, so a result can say whether
// it will start immediately or has to download first.
//
// One POST for the whole page - the route takes the batch precisely so a page of
// results doesn't become a page of round trips onto OpenKJ's Qt main thread. It
// checks at most 50 ids and ignores the rest, so the slice matches. A video
// nobody has ever requested appears in neither list, which is the ordinary case
// and reads as "will download when requested".
function okj_youtube_cached_status($videoIds)
{
  $status = array('ready' => array(), 'pending' => array());
  $videoIds = array_slice(array_values(array_unique($videoIds)), 0, 50);
  if (!count($videoIds)) return $status;

  $res = okj_post('/local/youtube/cached', array('videoIds' => $videoIds));
  // Older OpenKJ builds 404 this route. Badging is decoration - losing it costs
  // the singer nothing, so a failure just means no badges.
  if (!$res['ok']) return $status;

  foreach (array('ready', 'pending') as $bucket)
  {
    if (!isset($res['data'][$bucket]) || !is_array($res['data'][$bucket])) continue;
    foreach ($res['data'][$bucket] as $videoId) $status[$bucket][(string)$videoId] = true;
  }
  return $status;
}

// The API hands over its own reaction vocabulary precisely so clients lay out
// buttons for what this build accepts - anything else is rejected as "Unknown
// reaction". One we have no glyph for is a reaction a newer OpenKJ knows about
// and we don't, so it is skipped rather than drawn as a blank button.
function okj_cheer_reactions()
{
  $glyphs = okj_cheer_glyphs();
  $caps = okj_capabilities();
  $listed = (isset($caps['cheerReactions']) && is_array($caps['cheerReactions']))
    ? $caps['cheerReactions']
    : array_keys($glyphs);

  $reactions = array();
  foreach ($listed as $reaction)
  {
    if (isset($glyphs[$reaction])) $reactions[$reaction] = $glyphs[$reaction];
  }
  return $reactions;
}


/* ------------------------------------------------------------------ *
 * Output helpers
 * ------------------------------------------------------------------ */

function h($value)
{
  return htmlspecialchars((string)$value, ENT_QUOTES, 'UTF-8');
}

// JSON safe to sit inside a single-quoted hx-vals attribute.
function hxvals($values)
{
  return h(json_encode($values));
}

// A song's length as m:ss, or '' when the library doesn't know it. Lives here
// rather than in queue-render.inc because the song lists want it too.
//
// Unknown arrives as 1, not 0: every route floors the value with
// `std::max(1, COALESCE(ds.duration, 0) / 1000)`, so a library scanned without
// durations would otherwise read "0:01" against every song in it. Nothing in a
// karaoke library is under half a minute, so anything shorter is missing
// metadata rather than a short song, and saying nothing beats saying that.
function okj_fmt_duration($seconds)
{
  $seconds = (int)$seconds;
  if ($seconds < 30) return '';
  return sprintf('%d:%02d', intdiv($seconds, 60), $seconds % 60);
}

// The length as it rides inside a result button, so the three lists that offer
// songs render it identically. Returns markup rather than echoing it, the way
// okj_key_change() does, because it is composed into a button being built.
function okj_song_length($seconds)
{
  $length = okj_fmt_duration($seconds);
  if ($length === '') return '';
  return '<span class="song-len">' . $length . '</span>';
}

// htmx swaps a fragment into an existing page; a direct hit (or an hx-boost'ed
// navigation) needs the whole document. Pages render only their target
// fragment when this is true.
function okj_is_fragment()
{
  return isset($_SERVER['HTTP_HX_REQUEST']) && !isset($_SERVER['HTTP_HX_BOOSTED']);
}

function okj_event_name()
{
  global $venueName;
  static $name = null;
  if ($name !== null) return $name;

  $name = $venueName;
  // Cached per session, but not forever: a KJ who renames the event shouldn't
  // have to ask everyone to clear their cookies.
  if (isset($_SESSION['okj_event_name']) && isset($_SESSION['okj_event_name_until'])
      && $_SESSION['okj_event_name_until'] > time())
  {
    $name = $_SESSION['okj_event_name'];
    return $name;
  }
  $res = okj_get('/local/event-settings');
  if (!$res['ok']) return $name;

  if (!empty($res['data']['appName']) && $res['data']['appName'] !== 'OpenKJ')
  {
    $name = $res['data']['appName'];
  }
  $_SESSION['okj_event_name'] = $name;
  $_SESSION['okj_event_name_until'] = time() + 300;
  return $name;
}

function siteheader()
{
  $eventName = okj_event_name();
  echo "<html>
  <head>
    <meta charset=\"UTF-8\">
    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">
    <title>" . h($eventName) . " Songbook</title>

    <link rel=\"preconnect\" href=\"https://cdn.jsdelivr.net\" crossorigin>
    <link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/@xz/fonts@1.0.2/serve/metropolis.css\" integrity=\"sha256-EbF7uuhKup7x84hcfaVXj5gof+v/9hfv7Wsxud7vWyU=\" crossorigin=\"anonymous\">
    <link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/@fontsource/righteous@5.0.14/index.css\" integrity=\"sha256-3LZETteO5Rdt11DQIU0Uj0He3Tdr/JnfBZcsP87UifE=\" crossorigin=\"anonymous\">

    <link rel=\"stylesheet\" type=\"text/css\" href=\"css/style.css\" />

    <script defer src=\"https://cdn.jsdelivr.net/npm/htmx.org@1.9.6/dist/htmx.min.js\" integrity=\"sha256-y7cjwwXPbWMVyJCQmBVSNYhQni4JKln4z8SohYKWidU=\" crossorigin=\"anonymous\"></script>
  </head>
  <body
    hx-on:keydown=\"
      const reqModal = htmx.find('#req-modal');
      if (event.key === 'Escape' && reqModal)
        htmx.trigger(reqModal, 'remove-req-modal');
    \">";
}

// Subscribes to the API's SSE stream. Two kinds of frame arrive on the one
// connection:
//
//   "queue"  - JSON, not markup, so it can't be swapped in directly. It is used
//              purely as a "something moved, re-render" signal, which keeps the
//              rotation server-rendered exactly as before.
//   "tick"   - the countdowns moved and nothing else did. Deliberately ignored;
//              see the ?deltas=1 note below.
//   "upnext" - the API's own edge detection, one frame per singer as they come
//              within the KJ's alert window. Fanned out to every subscriber
//              because the stream carries no identity, so the filtering by name
//              below is what makes it the singer's own alert.
//   "cheer"  - the applause tally for the song being sung, batched every 250ms
//              while the room is tapping. Unlike "queue" this one carries the
//              value rather than signalling a re-render, because it fires far
//              too often to spend an API call on.
//
// Site-wide, from sitefooter(), rather than from the queue page: a singer is
// most likely to be searching for their next song when their turn comes round,
// and an alert that only fires on the page already telling them their position
// is an alert nobody needs. That is also why a missing #queue-target no longer
// closes the stream - most pages have none and still want "upnext".
//
// Emitted at the end of <body>, which is both after #queue-target exists to be
// looked up and outside it - that node is replaced wholesale on every refresh,
// and a script inside it would open a fresh EventSource each time.
function okj_event_stream_script()
{
  global $openkjEventStreamBase;

  $base = trim($openkjEventStreamBase);
  if ($base === '') return;

  // A path-only base leaves a relative URL here, which the browser resolves
  // against the page's own origin - that is what lets "/" mean "same origin",
  // and it is the only setting that doesn't require publishing port 5050
  // somewhere a phone can reach. Don't "simplify" the rtrim away: for the "/"
  // case it is what keeps this from becoming a protocol-relative "//local/...".
  //
  // ?deltas=1 opts into the API's smaller refresh frames. Without it, OpenKJ
  // pushes a whole snapshot to every subscriber every 20s whether or not the
  // show moved - and since a "queue" frame makes this page re-render, that was
  // /local/user/me + /local/queue per phone per 20s onto OpenKJ's Qt main
  // thread, faster than the fallback poll it was supposed to be replacing. With
  // it, "queue" arrives only when something actually changed; a tick where only
  // the countdowns moved comes as a "tick" frame and one where nothing moved as
  // an SSE comment, and neither wakes this page at all.
  //
  // Nothing listens for "tick" - EventSource drops an event with no listener,
  // so ignoring it costs a handler we'd have to write rather than an API call
  // we'd have to make. What it carries is wait_seconds keyed by request id, and
  // applying that here would mean re-implementing okj_fmt_wait() in JS to write
  // a number the rotation already rounds to the nearest minute. The estimates
  // instead go stale until the next real change or the $queueStreamPollSeconds
  // poll, which is the only page that shows them anyway.
  //
  // Hardcoded rather than read from /local/capabilities (queueDeltaQueryParam),
  // the same way the path itself is: an OpenKJ predating deltas ignores an
  // unknown query parameter and keeps sending full frames, so the cost of
  // guessing wrong is the behaviour we already had.
  $url = json_encode(rtrim($base, '/') . '/local/events?deltas=1');

  // Matched against the frame's singer name the way okj_entry_is_mine() does
  // it, folded here so the comparison in JS stays a plain ===. The flags matter
  // because this is a user-chosen string being written into a <script>.
  $user = okj_user();
  $me = json_encode(strtolower(trim($user['username'])),
    JSON_HEX_TAG | JSON_HEX_AMP | JSON_HEX_APOS | JSON_HEX_QUOT);

  echo '<script>
(function () {
  if (!window.EventSource) return;

  var me = ' . $me . ';
  var watchQueue = !!document.getElementById("queue-target");
  // Signed out and not looking at the rotation, no frame on this stream could
  // change anything on this page - so don\'t hold a socket open on OpenKJ\'s
  // main thread for it.
  if (me === "" && !watchQueue) return;

  // hx-boost swaps the body without a page load, so an earlier subscriber can
  // still be live here.
  if (window.okjQueueStream) { window.okjQueueStream.close(); }

  var stream = new EventSource(' . $url . ');
  var pending = null;
  var primed = false;
  window.okjQueueStream = stream;

  stream.addEventListener("queue", function () {
    // The stream opens with a snapshot of current state, which is exactly what
    // the page just rendered - refreshing on it would be a wasted API call.
    if (!primed) { primed = true; return; }

    if (pending) { clearTimeout(pending); }
    pending = setTimeout(function () {
      pending = null;
      // No rotation on this page. The stream stays open regardless - it is
      // still carrying this singer\'s "upnext".
      var el = document.getElementById("queue-target");
      if (el) { htmx.trigger(el, "queue-changed"); }
    }, 400);
  });

  stream.addEventListener("cheer", function (event) {
    // Only the rotation has a tally to move, and only while something is
    // playing - everywhere else this is a no-op, the way the "queue" handler
    // above is. Nothing is re-fetched: the frame carries the total outright,
    // which is the whole reason cheers get their own frame instead of riding a
    // queue snapshot. They arrive orders of magnitude more often than the
    // rotation changes, and a re-render per batch would put an ovation onto
    // OpenKJ\'s main thread.
    var value = document.getElementById("cheer-count-value");
    if (!value) return;

    var frame;
    try { frame = JSON.parse(event.data); } catch (e) { return; }
    var total = parseInt(frame.total, 10);
    if (!(total > 0)) return;

    // Authoritative, and it overwrites whatever this phone counted optimistically
    // for its own taps. counts (the per-reaction breakdown) is for the singer
    // screen\'s glyphs; there is nothing to draw with it here.
    value.textContent = total;
    document.getElementById("cheer-count").hidden = false;
  });

  stream.addEventListener("upnext", function (event) {
    if (me === "") return;

    var frame;
    try { frame = JSON.parse(event.data); } catch (e) { return; }
    if (String(frame.singer || "").trim().toLowerCase() !== me) return;

    // OpenKJ tells each singer once per lap, but it forgets everyone the moment
    // the last subscriber drops - so in a room with one phone in it, every
    // navigation re-arms an alert already delivered. Keyed on the request, this
    // is what stops the reconnect from replaying it.
    var seen = "okj-upnext-" + frame.request_id;
    try {
      if (sessionStorage.getItem(seen)) return;
      sessionStorage.setItem(seen, "1");
    } catch (e) { /* storage denied - better a repeat alert than none */ }

    showUpNext(frame);
  });

  function showUpNext(frame) {
    var existing = document.getElementById("upnext-banner");
    if (existing) { existing.parentNode.removeChild(existing); }

    var banner = document.createElement("div");
    banner.id = "upnext-banner";
    banner.setAttribute("role", "status");

    var turns = parseInt(frame.turns_until, 10) || 1;
    var headline = document.createElement("strong");
    headline.textContent = turns <= 1 ? "You\'re up next" : turns + " singers away";

    var song = document.createElement("span");
    song.className = "upnext-song";
    // textContent, never innerHTML: these are API strings landing in the DOM,
    // and h() cannot reach them out here.
    song.textContent = (frame.artist || "") + " - " + (frame.title || "");

    var link = document.createElement("a");
    link.href = "/queue.php";
    link.textContent = "Rotation";

    var dismiss = document.createElement("button");
    dismiss.type = "button";
    dismiss.className = "close";
    dismiss.textContent = "Dismiss";
    dismiss.addEventListener("click", function () {
      if (banner.parentNode) { banner.parentNode.removeChild(banner); }
    });

    banner.appendChild(headline);
    banner.appendChild(song);
    banner.appendChild(link);
    banner.appendChild(dismiss);
    document.body.appendChild(banner);
  }

  // An OpenKJ without /local/events answers 404, which EventSource treats as
  // fatal and never retries - the fallback poll takes it from there, and there
  // is no up-next alert at all on such a build.
  stream.addEventListener("error", function () {
    if (stream.readyState === EventSource.CLOSED && window.okjQueueStream === stream) {
      window.okjQueueStream = null;
    }
  });
})();
</script>';
}

function sitefooter() {
  echo "</main>";
  okj_event_stream_script();
  echo "</body></html>";
}

function navbar($active = 'songbook')
{
  $eventName = okj_event_name();
  $user = okj_user();

  echo "<header><h1><a href='/' hx-boost='true'>" . h($eventName) . " Songbook</a></h1>
    <nav hx-boost='true'>";
  $links = array('/' => 'Search', '/browse.php' => 'Browse');
  $keys = array('/' => 'songbook', '/browse.php' => 'browse');
  // Next to the songbook it supplements, and only when OpenKJ says it will
  // accept a video - a tab that can only ever fail is worse than no tab.
  if (okj_youtube_enabled())
  {
    $links['/youtube.php'] = 'YouTube';
    $keys['/youtube.php'] = 'youtube';
  }
  $links['/queue.php'] = 'Rotation';
  $keys['/queue.php'] = 'queue';
  // Favorites and history belong to an account, so the links are meaningless
  // signed out.
  if ($user['authenticated'])
  {
    $links['/favorites.php'] = 'Favorites';
    $keys['/favorites.php'] = 'favorites';
    $links['/history.php'] = 'History';
    $keys['/history.php'] = 'history';
  }
  foreach ($links as $href => $label)
  {
    $current = ($keys[$href] === $active) ? " class='current'" : "";
    echo "<a href='" . h($href) . "'$current>" . h($label) . "</a> ";
  }
  if ($user['authenticated'])
    echo "<a href='/account.php'" . ($active === 'account' ? " class='current'" : "") . ">" . h($user['username']) . "</a>";
  else
    echo "<a href='/account.php'" . ($active === 'account' ? " class='current'" : "") . ">Sign in</a>";
  echo "</nav></header><main>";
}

function errorBanner($message)
{
  if ($message === '') return;
  echo "<p class=\"api-error\">" . h($message) . "</p>";
}

// Shown when the KJ software isn't reachable at all - distinct from an empty
// result, because there is nothing the singer can do about it.
function offlineNotice($message)
{
  echo "<div class=\"offline\">
    <h2>Songbook unavailable</h2>
    <p>Can't reach the karaoke software right now. Please let your KJ know.</p>
    <p class=\"detail\">" . h($message) . "</p>
  </div>";
}


/* ------------------------------------------------------------------ *
 * Shared UI pieces
 * ------------------------------------------------------------------ */

function searchform($query = "")
{
  $disabled = ($query == "") ? "disabled" : "";
  echo '<div id="song-search-input-container"><input
      id="song-search-input"
      type="search"
      name="q"
      value="' . h($query) . '"
      autofocus
      autocomplete="off"
      placeholder="Type here to search songs"
      hx-get="/search.php"
      hx-sync="this:replace"
      hx-push-url="true"
      hx-trigger="input[this.value.trim().length > 2 || this.value.length === 0] changed delay:250ms, search"
      hx-target="#data-target"
      hx-select="#data-target"
      hx-indicator="#search-indicator"
      hx-on:clear-search="
        this.value = \'\';
        htmx.trigger(this, \'input\');
        htmx.find(\'#song-search-input-clear\').setAttribute(\'disabled\', \'true\');
      "
      hx-on:input="
        if (this.value.length > 0)
          htmx.find(\'#song-search-input-clear\').removeAttribute(\'disabled\');
        else
          htmx.find(\'#song-search-input-clear\').setAttribute(\'disabled\', \'true\');
        "
      >
      <button id="song-search-input-clear" type="reset" ' . $disabled . '
        hx-on:click="htmx.trigger(\'#song-search-input\', \'clear-search\')"
        >
        <svg xmlns="http://www.w3.org/2000/svg" fill="currentColor" class="bi bi-x-lg" viewBox="0 0 16 16">
          <path d="M2.146 2.854a.5.5 0 1 1 .708-.708L8 7.293l5.146-5.147a.5.5 0 0 1 .708.708L8.707 8l5.147 5.146a.5.5 0 0 1-.708.708L8 8.707l-5.146 5.147a.5.5 0 0 1-.708-.708L7.293 8 2.146 2.854Z"/>
        </svg>
      </button>
      </div>
      <div id="search-indicator" class="htmx-indicator">Searching&hellip;</div>';
}

function helpHints()
{
  echo '<details open>
    <summary>Instructions</summary>
    <p>Enter at least 3 characters (letters/numbers) as a search query.</p>
    <p>You may enter any part of the artist and/or title of the song, and partial words are allowed.</p>
    <p>For example, "pai bla stone" would match "Rolling Stones, The - Paint it black".</p>
    <p>Or <a href="/browse.php" hx-boost="true">browse the songbook by letter</a>.</p>
  </details>';
  if (!okj_accepting())
    echo '<details open>
      <summary>Requests closed</summary>
      <p>You will not be able to request songs at this time.</p>
    </details>';
}

// The star toggle. In a result list it swaps itself, so starring a song costs
// one small request and leaves the results alone; the favorites page passes
// render=list instead, because there the row has to disappear.
//
// $error renders the star in the state the song is *still* in - a toggle that
// failed must not show the state the tap asked for.
function favStar($songId, $isFavorite, $error = '', $render = 'toggle')
{
  $songId = (int)$songId;
  $vals = hxvals(array(
    'songid' => $songId,
    'action' => $isFavorite ? 'remove' : 'add',
    'render' => $render,
  ));
  $classes = 'fav-toggle' . ($isFavorite ? ' on' : '') . ($error !== '' ? ' error' : '');
  $title = $error !== '' ? $error : ($isFavorite ? 'Remove from favorites' : 'Save to favorites');
  $target = ($render === 'list') ? '#favorites-target' : 'this';

  echo "<button type=\"button\"
    class=\"" . h($classes) . "\"
    title=\"" . h($title) . "\"
    aria-pressed=\"" . ($isFavorite ? 'true' : 'false') . "\"
    hx-post=\"/favorite-action.php\"
    hx-target=\"" . h($target) . "\"
    hx-swap=\"outerHTML\"
    hx-vals='$vals'>" . ($isFavorite ? '&#9733;' : '&#9734;') . "</button>";
}

// The API has no lookup-song-by-id route, so artist & title ride along in
// hx-vals to spare the request modal an extra round trip.
function songButtons($songs, $accepting)
{
  // Signed out there is nothing to star, and the markup stays exactly as it
  // was - no row wrapper, no favorites lookup.
  $user = okj_user();
  $canStar = $user['authenticated'];
  $favorites = $canStar ? okj_favorite_ids() : array();

  foreach ($songs as $song)
  {
    $songId = isset($song['song_id']) ? (int)$song['song_id'] : 0;
    $artist = isset($song['artist']) ? $song['artist'] : '';
    $title = isset($song['title']) ? $song['title'] : '';
    $duration = isset($song['duration_seconds']) ? (int)$song['duration_seconds'] : 0;
    // The label is its own span so the length can sit opposite it: a flex
    // button lays out an anonymous text node too, but there is no selector to
    // stop that one from squeezing the length off the row.
    $label = '<span class="song-label">' . h($artist . ' - ' . $title) . '</span>'
      . okj_song_length($duration);
    $star = ($canStar && $songId > 0);

    if ($star) echo '<div class="result-row">';

    if (!$accepting || $songId < 1)
    {
      echo "<button class=\"result song\">$label</button>";
    }
    else
    {
      // duration rides along for the modal's benefit, the same way artist and
      // title do - there is no fetch-song-by-id route to look it up with.
      $vals = hxvals(array('songid' => $songId, 'artist' => $artist, 'title' => $title, 'duration' => $duration));
      echo "<button
        class=\"result song\"
        hx-post=\"/req-modal.php\"
        hx-target=\"body\"
        hx-swap=\"beforeend\"
        hx-vals='$vals'>$label</button>";
    }

    if ($star)
    {
      favStar($songId, isset($favorites[$songId]));
      echo '</div>';
    }
  }
}

function reqFormContent($songid = "", $artist = "", $title = "", $videoId = "", $durationSeconds = 0)
{
  $user = okj_user();
  echo "<input type=\"hidden\" name=\"songid\" value=\"" . h($songid) . "\">
  <input type=\"hidden\" name=\"artist\" value=\"" . h($artist) . "\">
  <input type=\"hidden\" name=\"title\" value=\"" . h($title) . "\">";

  // A YouTube request carries the video instead of a library song id, because
  // there is no library row to point at yet - OpenKJ creates one as a side
  // effect of accepting the request. The duration rides along for the same
  // reason it does on a library result: nothing here can look it up again, and
  // OpenKJ wants it to refuse an over-long video up front.
  if ($videoId !== '')
  {
    echo "<input type=\"hidden\" name=\"videoid\" value=\"" . h($videoId) . "\">
    <input type=\"hidden\" name=\"durationSeconds\" value=\"" . (int)$durationSeconds . "\">";
  }

  // Unlike a library song, a video cannot be requested by a guest at all:
  // /local/request/youtube authenticates before anything else. Saying so here
  // beats a name box whose submission is refused.
  if ($videoId !== '' && !$user['authenticated'])
  {
    echo "<p>Requesting from YouTube needs an account, so the download can be
      tied to whoever asked for it.</p>
      <div class=\"req-modal-buttons\">
        <button
          id=\"cancel-song-request\"
          type=\"button\"
          class=\"close\"
          hx-on:click=\"htmx.trigger('#req-modal', 'remove-req-modal');\"
          >Close</button>
        <a class=\"button\" href=\"/account.php\" hx-boost=\"true\">Sign in</a>
      </div>";
    return;
  }

  if ($user['authenticated'])
  {
    // Signed in: the request is filed under the account, which is what makes
    // it removable and re-orderable later.
    echo "<p>Requesting as <strong>" . h($user['username']) . "</strong></p>
      <div class=\"req-modal-buttons\">
        <button
          id=\"cancel-song-request\"
          type=\"button\"
          class=\"close\"
          hx-on:click=\"htmx.trigger('#req-modal', 'remove-req-modal');\"
          >Close</button>
        <button type=\"submit\" id=\"send-song-request\">Send Request</button>
      </div>";
    return;
  }

  echo "<label>Who will sing it?</label>
  <input type=\"text\" name=\"singer\"
    autocomplete=\"off\" autofocus
    placeholder=\"Enter your name or nickname\"
    hx-on:input=\"
      if (this.value.length > 0)
        htmx.find('#send-song-request').removeAttribute('disabled');
      else
        htmx.find('#send-song-request').setAttribute('disabled', 'true');
    \"
    >
  <p class=\"hint\"><a href=\"/account.php\" hx-boost=\"true\">Sign in</a> to manage your own songs later.</p>
  <div class=\"req-modal-buttons\">
    <button
      id=\"cancel-song-request\"
      type=\"button\"
      class=\"close\"
      hx-on:click=\"htmx.trigger('#req-modal', 'remove-req-modal');\"
      >Close</button>
    <button
      type=\"submit\"
      id=\"send-song-request\"
      disabled
      >Send Request</button>
  </div>";
}

?>
