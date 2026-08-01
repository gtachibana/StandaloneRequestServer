<?php

/* ------------------------------------------------------------------ *
 * YouTube search
 *
 * The one part of this app that talks to something other than OpenKJ.
 * /local/capabilities is explicit about why: "Clients search YouTube
 * themselves and POST the result to /local/request/youtube". OpenKJ owns
 * downloading and playing the video, but it has no search route at all - it
 * only ever sees a videoId a singer already picked. So the searching lands
 * here.
 *
 * Two backends, chosen by whether $youtubeApiKey is set:
 *
 *   key set   - the official Data API v3. Clean JSON, but search.list costs
 *               100 units of a 10,000/day quota, i.e. about a hundred searches
 *               a day for the whole venue. A busy night goes through that.
 *   key empty - fetch the ordinary results page and read the ytInitialData
 *               blob out of it. No key, no quota, and it is what the default
 *               is, because the quota above is the tighter constraint at a
 *               venue. It is also the brittle one: YouTube can move the shape
 *               of that blob whenever it likes, which is why the parsing is
 *               confined to this file and written to walk the tree for what it
 *               wants rather than follow a fixed path into it.
 *
 * Nothing here touches the OpenKJ API, and none of it is on the songbook's
 * hot path - only youtube.php calls in.
 * ------------------------------------------------------------------ */

// Outbound HTTP for this file only. okj_call() is the wrong tool: it speaks
// JSON to a host we control on a short timeout tuned for OpenKJ's Qt main
// thread, and this is HTML from the public internet.
function okj_yt_http_get($url, $timeout)
{
  $headers = array(
    // A browser UA and language. Served an unknown client, YouTube is within its
    // rights to hand back a page shaped differently to the one parsed below.
    'User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36',
    'Accept-Language: en-US,en;q=0.9',
    'Accept: text/html,application/json,*/*',
    // Without these, a request from an EU-adjacent egress is redirected to the
    // consent interstitial and the "results" that come back have no videos in
    // them at all. These are the cookies a consent choice leaves behind.
    'Cookie: CONSENT=YES+1; SOCS=CAI',
  );

  if (function_exists('curl_init'))
  {
    $ch = curl_init($url);
    curl_setopt_array($ch, array(
      CURLOPT_RETURNTRANSFER => true,
      CURLOPT_FOLLOWLOCATION => true,
      CURLOPT_MAXREDIRS => 3,
      CURLOPT_CONNECTTIMEOUT => 3,
      CURLOPT_TIMEOUT => $timeout,
      CURLOPT_HTTPHEADER => $headers,
      CURLOPT_ENCODING => '',
    ));
    $body = curl_exec($ch);
    $status = (int)curl_getinfo($ch, CURLINFO_RESPONSE_CODE);
    $error = curl_error($ch);
    curl_close($ch);
    if ($body === false) return array('ok' => false, 'body' => '', 'status' => 0,
      'error' => $error !== '' ? $error : 'Could not reach YouTube');
  }
  else
  {
    $context = stream_context_create(array('http' => array(
      'method' => 'GET',
      'header' => implode("\r\n", $headers) . "\r\n",
      'timeout' => $timeout,
      'follow_location' => 1,
      'max_redirects' => 3,
      'ignore_errors' => true,
    )));
    $body = @file_get_contents($url, false, $context);
    if ($body === false) return array('ok' => false, 'body' => '', 'status' => 0, 'error' => 'Could not reach YouTube');
    $status = 0;
    if (isset($http_response_header[0]) && preg_match('!HTTP/\S+\s+(\d+)!', $http_response_header[0], $m))
      $status = (int)$m[1];
  }

  if ($status < 200 || $status >= 300)
    return array('ok' => false, 'body' => '', 'status' => $status, 'error' => "YouTube returned HTTP $status");

  return array('ok' => true, 'body' => $body, 'status' => $status, 'error' => '');
}

// "4:15" or "1:02:33" to seconds. Anything else - an empty string, "LIVE",
// "SHORTS" - comes back 0, which the caller reads as "no usable duration".
function okj_yt_parse_clock($text)
{
  $text = trim((string)$text);
  if ($text === '' || !preg_match('/^(?:(\d+):)?(\d{1,2}):(\d{2})$/', $text, $m)) return 0;
  return ((int)$m[1] * 3600) + ((int)$m[2] * 60) + (int)$m[3];
}

// ISO 8601 durations, which is the form the Data API reports: PT4M15S.
function okj_yt_parse_iso8601($text)
{
  if (!preg_match('/^P(?:\d+D)?T(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?$/', trim((string)$text), $m)) return 0;
  return ((int)(isset($m[1]) ? $m[1] : 0) * 3600)
    + ((int)(isset($m[2]) ? $m[2] : 0) * 60)
    + (int)(isset($m[3]) ? $m[3] : 0);
}

// YouTube writes user-visible strings two ways in the same document -
// {"simpleText": "..."} and {"runs": [{"text": "..."}, ...]} - and which one a
// given field uses is not stable across page versions, so both are read.
function okj_yt_text($node)
{
  if (!is_array($node)) return '';
  if (isset($node['simpleText'])) return trim((string)$node['simpleText']);
  if (isset($node['runs']) && is_array($node['runs']))
  {
    $out = '';
    foreach ($node['runs'] as $run)
    {
      if (is_array($run) && isset($run['text'])) $out .= $run['text'];
    }
    return trim($out);
  }
  return '';
}

// Pulls the JavaScript object literal that follows $marker out of the HTML.
// Brace-counting rather than a regex, because the blob is ~1MB of JSON on one
// line and any non-greedy match either stops at the first '}' inside it or
// backtracks over the lot. String state is tracked so a '}' inside a video
// title can't end the scan early.
function okj_yt_extract_json($html, $marker)
{
  $at = strpos($html, $marker);
  if ($at === false) return null;
  $start = strpos($html, '{', $at + strlen($marker));
  if ($start === false) return null;

  $depth = 0;
  $inString = false;
  $escaped = false;
  $len = strlen($html);

  for ($i = $start; $i < $len; $i++)
  {
    $c = $html[$i];

    if ($inString)
    {
      if ($escaped) $escaped = false;
      elseif ($c === '\\') $escaped = true;
      elseif ($c === '"') $inString = false;
      continue;
    }

    if ($c === '"') { $inString = true; continue; }
    if ($c === '{') { $depth++; continue; }
    if ($c === '}')
    {
      if (--$depth === 0)
      {
        $decoded = json_decode(substr($html, $start, $i - $start + 1), true);
        return is_array($decoded) ? $decoded : null;
      }
    }
  }
  return null;
}

// Derived from the id rather than read off the search response. Both backends
// hand over a URL, but they hand over different ones - the results page carries
// signed hq720 links around 50KB each, which is a megabyte of images for one
// screen of results on a venue's wifi. mqdefault is a fixed 320x180, needs no
// signature, and is 16:9 already, so it neither letterboxes nor has to be
// cropped. Safe to build by hand because the id is validated first.
function okj_yt_thumb($videoId)
{
  return 'https://i.ytimg.com/vi/' . $videoId . '/mqdefault.jpg';
}

// Every videoRenderer in the tree, in document order.
//
// Deliberately a walk rather than the documented path
// (contents.twoColumnSearchResultsRenderer.primaryContents...): that path picks
// up a new wrapper layer every time YouTube ships a shelf, a chip bar or an ad
// slot, and each one breaks search silently. What is actually invariant is that
// a result is an object under the key "videoRenderer".
function okj_yt_collect_renderers($node, &$out)
{
  if (!is_array($node)) return;
  if (isset($node['videoRenderer']) && is_array($node['videoRenderer'])) $out[] = $node['videoRenderer'];
  foreach ($node as $child)
  {
    if (is_array($child)) okj_yt_collect_renderers($child, $out);
  }
}

// One renderer to the shape the rest of the app uses, or null when it isn't
// something a singer can be offered.
function okj_yt_from_renderer($renderer)
{
  $videoId = isset($renderer['videoId']) ? trim((string)$renderer['videoId']) : '';
  // Same test OpenKJ's isValidYoutubeVideoId() applies, applied early so a
  // mis-parse becomes a missing row here rather than a rejection at request time.
  if (!preg_match('/^[A-Za-z0-9_-]{11}$/', $videoId)) return null;

  $title = okj_yt_text(isset($renderer['title']) ? $renderer['title'] : null);
  if ($title === '') return null;

  $channel = okj_yt_text(isset($renderer['ownerText']) ? $renderer['ownerText'] : null);
  if ($channel === '') $channel = okj_yt_text(isset($renderer['longBylineText']) ? $renderer['longBylineText'] : null);
  if ($channel === '') $channel = okj_yt_text(isset($renderer['shortBylineText']) ? $renderer['shortBylineText'] : null);

  // Live streams and premieres carry no lengthText. They are dropped rather than
  // shown without a length: OpenKJ enforces its cap on the duration the client
  // sends, and yt-dlp has nothing finite to cache from a stream anyway.
  $seconds = okj_yt_parse_clock(okj_yt_text(isset($renderer['lengthText']) ? $renderer['lengthText'] : null));
  if ($seconds <= 0) return null;

  return array(
    'videoId' => $videoId,
    'title' => $title,
    'channel' => $channel,
    'durationSeconds' => $seconds,
    'thumb' => okj_yt_thumb($videoId),
  );
}

function okj_yt_search_scrape($query, $limit, $timeout)
{
  // sp=EgIQAQ%3D%3D is the "Type: Video" filter, which keeps channels,
  // playlists and shelves out of the renderer list before it is ever parsed.
  $url = 'https://www.youtube.com/results?' . http_build_query(
    array('search_query' => $query, 'sp' => 'EgIQAQ=='), '', '&', PHP_QUERY_RFC3986);

  $res = okj_yt_http_get($url, $timeout);
  if (!$res['ok']) return array('ok' => false, 'videos' => array(), 'error' => $res['error']);

  $data = okj_yt_extract_json($res['body'], 'ytInitialData');
  // Either YouTube changed the page or we were served an interstitial. Both are
  // the same thing to a singer, and neither is worth spelling out on a phone.
  if ($data === null)
    return array('ok' => false, 'videos' => array(), 'error' => "Couldn't read YouTube's results.");

  $renderers = array();
  okj_yt_collect_renderers($data, $renderers);

  $videos = array();
  $seen = array();
  foreach ($renderers as $renderer)
  {
    $video = okj_yt_from_renderer($renderer);
    if ($video === null || isset($seen[$video['videoId']])) continue;
    $seen[$video['videoId']] = true;
    $videos[] = $video;
    if (count($videos) >= $limit) break;
  }

  return array('ok' => true, 'videos' => $videos, 'error' => '');
}

function okj_yt_search_api($query, $limit, $key, $timeout)
{
  $searchUrl = 'https://www.googleapis.com/youtube/v3/search?' . http_build_query(array(
    'part' => 'snippet',
    'type' => 'video',
    'maxResults' => min(50, max(1, $limit)),
    'q' => $query,
    'key' => $key,
  ), '', '&', PHP_QUERY_RFC3986);

  $res = okj_yt_http_get($searchUrl, $timeout);
  if (!$res['ok']) return array('ok' => false, 'videos' => array(), 'error' => $res['error']);

  $data = json_decode($res['body'], true);
  if (!is_array($data) || !isset($data['items']) || !is_array($data['items']))
    return array('ok' => false, 'videos' => array(), 'error' => "Couldn't read YouTube's results.");

  $order = array();
  $partial = array();
  foreach ($data['items'] as $item)
  {
    $videoId = isset($item['id']['videoId']) ? trim((string)$item['id']['videoId']) : '';
    if (!preg_match('/^[A-Za-z0-9_-]{11}$/', $videoId) || isset($partial[$videoId])) continue;
    $order[] = $videoId;
    $partial[$videoId] = array(
      'videoId' => $videoId,
      // The Data API HTML-escapes these ("Don&#39;t Stop Believin&#39;"), unlike
      // the ytInitialData blob, so they are decoded once here and everything
      // downstream - h(), the hx-vals payload, OpenKJ - sees plain text.
      'title' => html_entity_decode(isset($item['snippet']['title']) ? $item['snippet']['title'] : '', ENT_QUOTES, 'UTF-8'),
      'channel' => html_entity_decode(isset($item['snippet']['channelTitle']) ? $item['snippet']['channelTitle'] : '', ENT_QUOTES, 'UTF-8'),
      'durationSeconds' => 0,
      'thumb' => okj_yt_thumb($videoId),
    );
  }
  if (!count($order)) return array('ok' => true, 'videos' => array(), 'error' => '');

  // search.list doesn't report durations and OpenKJ needs one to enforce its
  // length cap, so this second call is not optional. It is cheap - videos.list
  // is 1 unit against the same quota search.list just spent 100 of.
  $videosUrl = 'https://www.googleapis.com/youtube/v3/videos?' . http_build_query(array(
    'part' => 'contentDetails',
    'id' => implode(',', $order),
    'key' => $key,
  ), '', '&', PHP_QUERY_RFC3986);

  $durationsRes = okj_yt_http_get($videosUrl, $timeout);
  if ($durationsRes['ok'])
  {
    $durations = json_decode($durationsRes['body'], true);
    if (is_array($durations) && isset($durations['items']) && is_array($durations['items']))
    {
      foreach ($durations['items'] as $item)
      {
        $videoId = isset($item['id']) ? (string)$item['id'] : '';
        if (!isset($partial[$videoId])) continue;
        $partial[$videoId]['durationSeconds'] = okj_yt_parse_iso8601(
          isset($item['contentDetails']['duration']) ? $item['contentDetails']['duration'] : '');
      }
    }
  }

  $videos = array();
  foreach ($order as $videoId)
  {
    // A zero duration is a live stream, a premiere, or the videos.list call
    // having failed. Dropped for the same reason as in the scraper.
    if ($partial[$videoId]['durationSeconds'] <= 0) continue;
    $videos[] = $partial[$videoId];
    if (count($videos) >= $limit) break;
  }

  return array('ok' => true, 'videos' => $videos, 'error' => '');
}

// What the venue actually types versus what gets searched. Singers type the song
// they want to sing; on YouTube that query returns the studio recording, which
// is useless to them and, worse, requestable. Appending the suffix is the
// difference between a tab that works and a tab that fills the rotation with
// original tracks nobody can sing over.
function okj_yt_effective_query($query)
{
  global $youtubeSearchSuffix;

  $suffix = isset($youtubeSearchSuffix) ? trim($youtubeSearchSuffix) : '';
  if ($suffix === '') return $query;
  // Skipped when they already said it, so "purple rain karaoke" doesn't become
  // "purple rain karaoke karaoke".
  if (stripos($query, $suffix) !== false) return $query;
  return $query . ' ' . $suffix;
}

// Session-cached, briefly. The page re-renders on navigation and on a failed
// request going back to the form, and each of those would otherwise be a fresh
// round trip to YouTube - slow on a phone, and quota against the key when one is
// configured. Held per session rather than shared, since there is nowhere shared
// to put it without a datastore this app deliberately doesn't have.
function okj_yt_search($query)
{
  global $youtubeApiKey, $youtubeSearchLimit, $youtubeSearchTimeout;

  $query = trim(preg_replace('!\s+!', ' ', (string)$query));
  if ($query === '') return array('ok' => true, 'videos' => array(), 'error' => '');

  $limit = isset($youtubeSearchLimit) ? max(1, (int)$youtubeSearchLimit) : 15;
  $timeout = isset($youtubeSearchTimeout) ? max(2, (int)$youtubeSearchTimeout) : 8;
  $key = isset($youtubeApiKey) ? trim($youtubeApiKey) : '';
  $effective = okj_yt_effective_query($query);
  $cacheKey = md5(($key !== '' ? 'api' : 'web') . '|' . $limit . '|' . $effective);

  if (isset($_SESSION['okj_yt_cache'][$cacheKey])
      && $_SESSION['okj_yt_cache'][$cacheKey]['until'] > time())
  {
    return array('ok' => true, 'videos' => $_SESSION['okj_yt_cache'][$cacheKey]['videos'], 'error' => '');
  }

  $res = ($key !== '')
    ? okj_yt_search_api($effective, $limit, $key, $timeout)
    : okj_yt_search_scrape($effective, $limit, $timeout);

  // Failures aren't cached - the next search should try YouTube again rather
  // than repeat a network blip back at the singer for five minutes.
  if (!$res['ok']) return $res;

  if (!isset($_SESSION['okj_yt_cache']) || !is_array($_SESSION['okj_yt_cache']))
    $_SESSION['okj_yt_cache'] = array();
  // Capped: this sits in the session file, and a singer typing for a while would
  // otherwise accumulate every query they passed through on the way.
  if (count($_SESSION['okj_yt_cache']) >= 8) $_SESSION['okj_yt_cache'] = array();
  $_SESSION['okj_yt_cache'][$cacheKey] = array('videos' => $res['videos'], 'until' => time() + 300);

  return $res;
}

?>
