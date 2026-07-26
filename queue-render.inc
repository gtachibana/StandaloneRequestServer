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

  echo '<ol class="queue-list">';
  foreach ($entries as $i => $entry)
  {
    $mine = okj_entry_is_mine($entry, $user);
    $classes = 'queue-entry' . ($mine ? ' mine' : '');
    if (!empty($entry['singer_paused'])) $classes .= ' away';

    echo '<li class="' . $classes . '">';
    echo '<span class="singer">' . h(isset($entry['singer']) ? $entry['singer'] : '') . '</span>';
    if (!empty($entry['singer_paused'])) echo ' <span class="away-tag">away</span>';
    echo '<span class="song">' . h((isset($entry['artist']) ? $entry['artist'] : '') . ' - ' . (isset($entry['title']) ? $entry['title'] : '')) . '</span>';
    echo '<span class="meta">' . okj_fmt_duration(isset($entry['duration_seconds']) ? $entry['duration_seconds'] : 0)
      . okj_key_change(isset($entry['key_change']) ? $entry['key_change'] : 0) . '</span>';
    if ($mine) okj_own_song_controls($entry, $i === $firstMine, $i === $lastMine);
    echo '</li>';
  }
  echo '</ol>';
}

// The whole polled region, including the wrapper that carries the poll trigger.
function renderQueueFragment($actionError = '')
{
  global $queuePollSeconds;

  $user = okj_user();
  $res = okj_get('/local/queue');

  echo '<div id="queue-target"
    hx-get="/queue.php"
    hx-trigger="every ' . (int)$queuePollSeconds . 's"
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

?>
