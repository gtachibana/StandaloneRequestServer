<?php

function okj_fmt_duration($seconds)
{
  $seconds = max(0, (int)$seconds);
  return sprintf('%d:%02d', intdiv($seconds, 60), $seconds % 60);
}

function okj_key_change($keyChange)
{
  $keyChange = (int)$keyChange;
  if ($keyChange === 0) return '';
  return ' <span class="keychg">' . ($keyChange > 0 ? '+' : '') . $keyChange . '</span>';
}

// wait_seconds is an estimate built from the durations of everything ahead, so
// round it hard - a "12 min" that is really 11:37 reads better than false
// precision that the next now-playing track blows away anyway.
function okj_fmt_wait($seconds)
{
  $seconds = max(0, (int)$seconds);
  if ($seconds < 60) return 'under a minute';

  $minutes = (int)round($seconds / 60);
  if ($minutes < 60) return 'about ' . $minutes . ' min';

  $hours = intdiv($minutes, 60);
  $rest = $minutes % 60;
  return 'about ' . $hours . 'h' . ($rest ? ' ' . $rest . 'm' : '');
}

// turns_until counts rotation positions, not songs: 0 is the singer holding the
// mic right now.
function okj_fmt_turns($turns)
{
  $turns = (int)$turns;
  if ($turns <= 0) return 'at the mic';
  if ($turns === 1) return 'up next';
  return $turns . ' singers away';
}

function okj_entry_is_mine($entry, $user)
{
  if (!$user['authenticated']) return false;
  $singer = isset($entry['singer']) ? $entry['singer'] : '';
  return strcasecmp(trim($singer), trim($user['username'])) === 0;
}

// entryId has to go over the wire as a JSON *string*: the API reads it with
// value("entryId").toString().toInt(), which yields 0 for a JSON number.
function okj_own_song_controls($entry, $isFirst, $isLast)
{
  $entryId = (string)(isset($entry['request_id']) ? $entry['request_id'] : 0);
  echo '<span class="mine-controls">';
  echo '<button title="Move up"' . ($isFirst ? ' disabled' : '') . '
    hx-post="/queue-action.php"
    hx-vals=\'' . hxvals(array('action' => 'up', 'entryId' => $entryId)) . '\'>&uarr;</button>';
  echo '<button title="Move down"' . ($isLast ? ' disabled' : '') . '
    hx-post="/queue-action.php"
    hx-vals=\'' . hxvals(array('action' => 'down', 'entryId' => $entryId)) . '\'>&darr;</button>';
  echo '<button title="Remove" class="remove"
    hx-post="/queue-action.php"
    hx-vals=\'' . hxvals(array('action' => 'remove', 'entryId' => $entryId)) . '\'>&times;</button>';
  echo '</span>';
}

function okj_render_queue_entries($entries, $user)
{
  // Position within the singer's own run of songs decides whether up/down are
  // available - the API's move is per-singer, so it can never change the
  // singer's place in the rotation.
  $mineIndexes = array();
  foreach ($entries as $i => $entry)
  {
    if (okj_entry_is_mine($entry, $user)) $mineIndexes[] = $i;
  }
  $firstMine = count($mineIndexes) ? $mineIndexes[0] : -1;
  $lastMine = count($mineIndexes) ? $mineIndexes[count($mineIndexes) - 1] : -1;

  // turns_until/wait_seconds describe the *singer's* next turn, so every song a
  // singer has queued carries the same pair. Showing it once, on their first
  // row, is the only reading that isn't a lie about the songs below it.
  $seenSingers = array();

  echo '<ol class="queue-list">';
  foreach ($entries as $i => $entry)
  {
    $mine = okj_entry_is_mine($entry, $user);
    $singerKey = strtolower(trim(isset($entry['singer']) ? $entry['singer'] : ''));
    $firstForSinger = !isset($seenSingers[$singerKey]);
    $seenSingers[$singerKey] = true;
    $classes = 'queue-entry' . ($mine ? ' mine' : '');
    if (!empty($entry['singer_paused'])) $classes .= ' away';

    echo '<li class="' . $classes . '">';
    echo '<span class="singer">' . h(isset($entry['singer']) ? $entry['singer'] : '') . '</span>';
    if (!empty($entry['singer_paused'])) echo ' <span class="away-tag">away</span>';
    echo '<span class="song">' . h((isset($entry['artist']) ? $entry['artist'] : '') . ' - ' . (isset($entry['title']) ? $entry['title'] : '')) . '</span>';
    echo '<span class="meta">' . okj_fmt_duration(isset($entry['duration_seconds']) ? $entry['duration_seconds'] : 0)
      . okj_key_change(isset($entry['key_change']) ? $entry['key_change'] : 0) . '</span>';
    // Absent on OpenKJ builds older than the ETA fields; the row just omits it.
    if ($firstForSinger && array_key_exists('turns_until', $entry))
    {
      $wait = isset($entry['wait_seconds']) ? (int)$entry['wait_seconds'] : 0;
      echo '<span class="eta">' . h(okj_fmt_turns($entry['turns_until']))
        . ($wait > 0 ? ' &middot; ' . h(okj_fmt_wait($wait)) : '') . '</span>';
    }
    if ($mine) okj_own_song_controls($entry, $i === $firstMine, $i === $lastMine);
    echo '</li>';
  }
  echo '</ol>';
}

// "You're up next" - the whole point of the ETA fields. Driven off the signed-in
// singer's earliest entry, since that is the turn that is actually coming.
function okj_your_turn_notice($entries, $user)
{
  if (!$user['authenticated']) return;

  foreach ($entries as $entry)
  {
    if (!okj_entry_is_mine($entry, $user)) continue;
    if (!array_key_exists('turns_until', $entry)) return;

    $turns = (int)$entry['turns_until'];
    $wait = isset($entry['wait_seconds']) ? (int)$entry['wait_seconds'] : 0;
    $when = $wait > 0 ? ' &mdash; ' . h(okj_fmt_wait($wait)) : '';

    if ($turns <= 0)
      echo '<p class="notice your-turn soon"><strong>You&rsquo;re up.</strong> Head for the mic.</p>';
    elseif ($turns === 1)
      echo '<p class="notice your-turn soon"><strong>You&rsquo;re up next</strong>' . $when . '.</p>';
    else
      echo '<p class="notice your-turn">' . $turns . ' singers ahead of you' . $when . '.</p>';
    return;
  }
}

// The whole polled region, including the wrapper that carries the refresh
// triggers. Two of them: the `queue-changed` event that the /local/events
// subscriber in queueStreamScript() fires, and a timed poll as the fallback.
function renderQueueFragment($actionError = '')
{
  global $queuePollSeconds, $queueStreamPollSeconds, $openkjEventStreamBase;

  $streaming = trim($openkjEventStreamBase) !== '';
  $pollSeconds = (int)($streaming ? $queueStreamPollSeconds : $queuePollSeconds);

  $user = okj_user();
  $res = okj_get('/local/queue');

  echo '<div id="queue-target"
    hx-get="/queue.php"
    hx-trigger="every ' . $pollSeconds . 's, queue-changed"
    hx-sync="this:replace"
    hx-target="this"
    hx-swap="outerHTML"
    hx-select="#queue-target">';

  errorBanner($actionError);

  if (!$res['ok'] && $res['status'] === 0)
  {
    offlineNotice($res['error']);
    echo '</div>';
    return;
  }
  if (!$res['ok'])
  {
    errorBanner($res['error']);
    echo '</div>';
    return;
  }

  $data = $res['data'];
  $nowPlaying = (isset($data['now_playing']) && is_array($data['now_playing'])) ? $data['now_playing'] : array();
  $upNext = (isset($data['up_next']) && is_array($data['up_next'])) ? $data['up_next'] : array();
  $recent = (isset($data['recently_played']) && is_array($data['recently_played'])) ? $data['recently_played'] : array();

  if (!empty($data['queueLocked']))
    echo '<p class="notice">Requests are closed right now.</p>';

  okj_your_turn_notice($upNext, $user);

  if ($user['authenticated'] && $user['inRotation'])
  {
    $away = $user['away'];
    echo '<div class="away-toggle">';
    echo $away
      ? '<p><strong>You&rsquo;re marked away</strong> &mdash; the rotation will skip you until you&rsquo;re back.</p>'
      : '<p>Stepping out for a bit? Mark yourself away and the rotation will skip you.</p>';
    echo '<button hx-post="/queue-action.php" hx-vals=\''
      . hxvals(array('action' => 'away', 'away' => $away ? '0' : '1')) . '\'>'
      . ($away ? 'I&rsquo;m back' : 'I&rsquo;ve stepped away') . '</button>';
    echo '</div>';
  }

  if ($nowPlaying)
  {
    echo '<section class="now-playing"><h2>Now singing</h2>';
    echo '<p class="singer">' . h(isset($nowPlaying['singer']) ? $nowPlaying['singer'] : '') . '</p>';
    echo '<p class="song">' . h((isset($nowPlaying['artist']) ? $nowPlaying['artist'] : '') . ' - ' . (isset($nowPlaying['title']) ? $nowPlaying['title'] : '')) . '</p>';
    echo '</section>';
  }

  echo '<section class="up-next"><h2>Up next</h2>';
  if (count($upNext))
  {
    okj_render_queue_entries($upNext, $user);
    if (!$user['authenticated'])
      echo '<p class="hint"><a href="/account.php" hx-boost="true">Sign in</a> to reorder or remove your own songs.</p>';
  }
  else
  {
    echo '<p>Nothing queued up yet.</p>';
  }
  echo '</section>';

  if (count($recent))
  {
    echo '<details class="recently-played"><summary>Recently played</summary><ol class="queue-list">';
    foreach ($recent as $entry)
    {
      echo '<li class="queue-entry"><span class="singer">' . h(isset($entry['singer']) ? $entry['singer'] : '') . '</span>';
      echo '<span class="song">' . h((isset($entry['artist']) ? $entry['artist'] : '') . ' - ' . (isset($entry['title']) ? $entry['title'] : '')) . '</span></li>';
    }
    echo '</ol></details>';
  }

  echo '</div>';
}

// Subscribes to the API's SSE stream and turns each frame into a `queue-changed`
// event on #queue-target. The frames carry JSON, not markup, so they can't be
// swapped in directly - they're used purely as a "something moved, re-render"
// signal, which keeps the rotation server-rendered exactly as before.
//
// Belongs outside #queue-target: that node is replaced wholesale on every
// refresh, and a script inside it would open a fresh EventSource each time.
function queueStreamScript()
{
  global $openkjEventStreamBase;

  $base = trim($openkjEventStreamBase);
  if ($base === '') return;

  // A path-only base leaves a relative URL here, which the browser resolves
  // against the page's own origin - that is what lets "/" mean "same origin",
  // and it is the only setting that doesn't require publishing port 5050
  // somewhere a phone can reach. Don't "simplify" the rtrim away: for the "/"
  // case it is what keeps this from becoming a protocol-relative "//local/...".
  $url = json_encode(rtrim($base, '/') . '/local/events');
  echo '<script>
(function () {
  if (!window.EventSource) return;
  // hx-boost swaps the body without a page load, so an earlier subscriber can
  // still be live here.
  if (window.okjQueueStream) { window.okjQueueStream.close(); }

  var stream = new EventSource(' . $url . ');
  var pending = null;
  var primed = false;
  window.okjQueueStream = stream;

  function shutdown() {
    stream.close();
    if (window.okjQueueStream === stream) { window.okjQueueStream = null; }
  }

  stream.addEventListener("queue", function () {
    // The stream opens with a snapshot of current state, which is exactly what
    // the page just rendered - refreshing on it would be a wasted API call.
    if (!primed) { primed = true; return; }

    var target = document.getElementById("queue-target");
    if (!target) { shutdown(); return; }

    if (pending) { clearTimeout(pending); }
    pending = setTimeout(function () {
      pending = null;
      var el = document.getElementById("queue-target");
      if (el) { htmx.trigger(el, "queue-changed"); } else { shutdown(); }
    }, 400);
  });

  // An OpenKJ without /local/events answers 404, which EventSource treats as
  // fatal and never retries - the fallback poll takes it from there.
  stream.addEventListener("error", function () {
    if (stream.readyState === EventSource.CLOSED && window.okjQueueStream === stream) {
      window.okjQueueStream = null;
    }
  });
})();
</script>';
}

?>
