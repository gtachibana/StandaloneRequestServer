<?php

// okj_fmt_duration() moved to global.inc when the song lists started showing
// lengths too.

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

// Pitch controls for the song being sung *right now*, shown only to the singer
// singing it. The API takes a direction rather than a value, so a tap composes
// with wherever the key actually is - the KJ can move the desktop spinbox
// without the snapshot we rendered from ever hearing about it.
//
// Everything inside #queue-target inherits its hx-target, hx-select, hx-swap
// and hx-sync, so each button has to override all four. Left alone, a key tap
// would try to pull a #queue-target out of a response that is only this
// control, swap the whole rotation with the result, and abort (or be aborted
// by) the queue's own poll.
//
// $error re-renders at the key the song is *still* in - never the one the tap
// asked for - the same rule favStar() follows for a failed toggle.
function okj_key_controls($keyChange, $error = '')
{
  $keyChange = max(-12, min(12, (int)$keyChange));
  $label = $keyChange === 0 ? 'Original key' : 'Key ' . ($keyChange > 0 ? '+' : '') . $keyChange;

  echo '<div id="key-controls" class="key-controls' . ($error !== '' ? ' error' : '') . '">';
  echo '<span class="key-label">' . h($label) . '</span>';
  if ($error !== '') echo '<span class="key-error">' . h($error) . '</span>';

  // The API clamps to +/-12 and reports at_limit, but the stop is derivable
  // from the value we are already holding - no need to round-trip to find out
  // a button does nothing.
  $buttons = array(
    'down' => array('&minus;', 'Lower the key', $keyChange <= -12),
    'up' => array('+', 'Raise the key', $keyChange >= 12),
  );
  foreach ($buttons as $direction => $button)
  {
    echo '<button type="button"
      title="' . h($button[1]) . '"
      aria-label="' . h($button[1]) . '"' . ($button[2] ? ' disabled' : '') . '
      hx-post="/key-action.php"
      hx-target="#key-controls"
      hx-swap="outerHTML"
      hx-select="unset"
      hx-sync="#key-controls:queue all"
      hx-vals=\'' . hxvals(array('direction' => $direction, 'current' => (string)$keyChange)) . '\'
      >' . $button[0] . '</button>';
  }
  echo '</div>';
}

// Bailing out of a bad pick without having to catch the KJ's eye. Pointedly not
// the KJ's own stop button: to everyone else this looks exactly like the track
// running out, and the rotation advances normally.
//
// is_playing is what makes the control safe to offer. The now-playing row comes
// from queuesongs.played = 1, so it keeps naming the last singer through the gap
// between one song ending and the next starting - a tap landing in there would
// otherwise stop the track the KJ has just started for somebody else. The API
// refuses it anyway, but a button that can only fail is worse than no button.
// It is absent entirely on OpenKJ builds without the skip route, hence
// array_key_exists(), the same gate key_change gets.
//
// Unlike the key nudge this goes through queue-action.php and re-renders the
// whole rotation, inheriting #queue-target's four htmx attributes to do it: it
// is tapped once rather than several times in a row, and what the singer wants
// to see next is the rotation moving on without them.
function okj_skip_control($nowPlaying)
{
  if (!array_key_exists('is_playing', $nowPlaying) || empty($nowPlaying['is_playing'])) return;

  // No entryId: the API works out for itself what is playing and whether it
  // belongs to this token, which is the only reading of "who is at the mic"
  // that can be trusted.
  echo '<div class="skip-control">
    <button type="button" class="skip"
      hx-post="/queue-action.php"
      hx-confirm="End your song now? The rotation moves on to the next singer."
      hx-vals=\'' . hxvals(array('action' => 'skip')) . '\'>End my song</button>
    </div>';
}

// Applause for the singer at the mic. No account needed - anyone in the room
// with the page open can tap - so this is the one control here that renders for
// a signed-out phone too.
//
// Support is gated twice, because the two failure modes look nothing alike. An
// OpenKJ predating cheers has no `cheers` key in now_playing at all, which is
// the free check and the same gate key_change and is_playing get. A build that
// has them but whose KJ switched them off still reports the tally, so only
// /local/capabilities can say - hence okj_cheers_enabled().
//
// "You can't cheer yourself" is this app's manners, not the API's rule:
// /local/cheer takes no token, so nothing stops a determined singer cheering
// themselves with curl. The point is only that we don't hand somebody a button
// to applaud their own performance. It follows that a singer who never signed
// in gets the buttons anyway - the stream and the rotation both identify people
// by name, and an anonymous phone has no name to match.
//
// They do get the tally. It is the reason OpenKJ carries it in now_playing at
// all: the singer is facing the room with the screen behind them, and this is
// the only way they see the room reacting.
function okj_cheer_bar($nowPlaying, $user)
{
  if (!array_key_exists('cheers', $nowPlaying)) return;
  if (!okj_cheers_enabled()) return;

  $reactions = okj_cheer_reactions();
  if (!count($reactions)) return;

  $total = (int)$nowPlaying['cheers'];
  $mine = okj_entry_is_mine($nowPlaying, $user);

  echo '<div id="cheer-bar" class="cheer-bar">';

  // The badge OpenKJ draws in the corner of the singer screen, mirrored: clap
  // glyph plus the running total. Hidden at zero rather than reading "0" -
  // nobody needs telling that the room hasn't reacted yet. The id is what the
  // /local/events "cheer" handler writes into, so the count follows the whole
  // room live instead of waiting for the next queue poll.
  echo '<span class="cheer-count" id="cheer-count" title="Cheers for this song"'
    . ($total > 0 ? '' : ' hidden') . '>&#x1F44F; <b id="cheer-count-value">'
    . $total . '</b></span>';

  if ($mine)
  {
    echo '</div>';
    return;
  }

  echo '<div class="cheer-buttons">';
  foreach ($reactions as $reaction => $glyph)
  {
    // Fire and forget: hx-swap="none", because there is nothing worth swapping
    // and nothing worth reporting. A throttled tap is meant to be swallowed, a
    // rejection can only be a stale capabilities cache (which cheer-action.php
    // fixes for the next render), and an unreachable API is already announced
    // by the rotation around this. What that buys is the smallest request in
    // the app for what is by far its most-tapped button.
    //
    // The count is bumped locally on the way out so the button feels alive even
    // with no event stream configured. It is optimistic and can run a little
    // ahead of the truth - the next "cheer" frame (250ms) or queue poll
    // overwrites it with the room's real total.
    //
    // All four attributes inherited from #queue-target have to go: hx-select
    // would hunt for a #queue-target in an empty body, and the inherited
    // hx-sync="this:replace" would have every tap and the queue's own poll
    // aborting each other. "queue all" rather than replace because each tap is
    // a distinct cheer that should reach the API, not a request for the latest
    // state that a newer one supersedes.
    echo '<button type="button" class="cheer"
      title="' . h($glyph[1]) . '"
      aria-label="' . h($glyph[1]) . '"
      hx-post="/cheer-action.php"
      hx-target="this"
      hx-swap="none"
      hx-select="unset"
      hx-sync="this:queue all"
      hx-vals=\'' . hxvals(array('reaction' => $reaction)) . '\'
      hx-on:click="
        const count = htmx.find(\'#cheer-count\');
        const value = htmx.find(\'#cheer-count-value\');
        if (value) {
          value.textContent = (parseInt(value.textContent, 10) || 0) + 1;
          count.hidden = false;
        }
        this.classList.remove(\'pop\');
        void this.offsetWidth;
        this.classList.add(\'pop\');
      "
      >' . $glyph[0] . '</button>';
  }
  echo '</div></div>';
}

function okj_entry_is_mine($entry, $user)
{
  if (!$user['authenticated']) return false;
  $singer = isset($entry['singer']) ? $entry['singer'] : '';
  return strcasecmp(trim($singer), trim($user['username'])) === 0;
}

// entryId has to go over the wire as a JSON *string*: the API reads it with
// value("entryId").toString().toInt(), which yields 0 for a JSON number.
//
// $canMove is false for a singer holding exactly one song, where both arrows
// would render disabled: two greyed-out arrows read as "you may not move up the
// rotation, sorry" rather than "there is nothing to reorder", and that is the
// most common case in the room.
function okj_own_song_controls($entry, $isFirst, $isLast, $canMove)
{
  $entryId = (string)(isset($entry['request_id']) ? $entry['request_id'] : 0);
  echo '<span class="mine-controls">';
  if ($canMove)
  {
    echo '<button title="Sing this sooner" aria-label="Sing this sooner"' . ($isFirst ? ' disabled' : '') . '
      hx-post="/queue-action.php"
      hx-vals=\'' . hxvals(array('action' => 'up', 'entryId' => $entryId)) . '\'>&uarr;</button>';
    echo '<button title="Sing this later" aria-label="Sing this later"' . ($isLast ? ' disabled' : '') . '
      hx-post="/queue-action.php"
      hx-vals=\'' . hxvals(array('action' => 'down', 'entryId' => $entryId)) . '\'>&darr;</button>';
  }
  echo '<button title="Remove this song" aria-label="Remove this song" class="remove"
    hx-post="/queue-action.php"
    hx-vals=\'' . hxvals(array('action' => 'remove', 'entryId' => $entryId)) . '\'>&times;</button>';
  echo '</span>';
}

// Whether the file behind a queued song is actually on disk yet. Only songs
// backed by a YouTube request carry these keys - a library song is on disk by
// definition, and an OpenKJ too old for the feature sends neither - so a row
// without them renders exactly as it always did.
//
// Returns markup rather than echoing it, like okj_key_change(), because it is
// composed into a meta line being built. "ready" says nothing on purpose: every
// other row in the list is ready too, and a badge on all of them is noise.
function okj_media_state($entry)
{
  if (!array_key_exists('media_state', $entry)) return '';

  switch ($entry['media_state'])
  {
    case 'pending':
      return '<span class="media-state pending">queued to download</span>';
    case 'fetching':
      $progress = isset($entry['media_progress']) ? (int)$entry['media_progress'] : 0;
      return '<span class="media-state pending">downloading'
        . ($progress > 0 ? ' ' . $progress . '%' : '') . '</span>';
    case 'failed':
      // Worth interrupting for: the song is in the rotation and will not play,
      // and the singer is the only one who can pick something else.
      return '<span class="media-state failed">download failed &mdash; pick another</span>';
  }
  return '';
}

// The singer's own songs, pulled out of the rotation into a list that contains
// nothing else. The reorder arrows used to live on the rotation rows, where an
// up arrow sitting in a list of everybody's songs reads as "move me up the
// rotation" - which is not what /local/request/move does, and not something the
// API offers at all. In a list holding only this singer's songs the scope needs
// no explaining.
//
// Costs no extra API call: this renders from the same /local/queue response the
// rotation below it is already built from, which matters because that response
// is served off OpenKJ's Qt main thread.
//
// The song at the mic is deliberately absent - it is already its own section
// up the page, with the controls that apply to a song in progress (key, skip)
// rather than the ones that apply to a song still waiting.
function okj_render_my_queue($entries, $user)
{
  if (!$user['authenticated']) return;

  $mine = array();
  foreach ($entries as $entry)
  {
    if (okj_entry_is_mine($entry, $user)) $mine[] = $entry;
  }
  if (!count($mine)) return;

  $canMove = count($mine) > 1;
  $last = count($mine) - 1;

  echo '<section class="my-queue"><h2>My Queue</h2>';
  echo '<p class="hint">' . ($canMove
    ? 'You&rsquo;ll sing these in this order. Reordering them doesn&rsquo;t change your place in the rotation.'
    : 'Your next song.') . '</p>';
  echo '<ol class="queue-list my-queue-list">';
  foreach ($mine as $i => $entry)
  {
    echo '<li class="queue-entry mine">';
    echo '<span class="song">' . h((isset($entry['artist']) ? $entry['artist'] : '') . ' - ' . (isset($entry['title']) ? $entry['title'] : '')) . '</span>';
    echo '<span class="meta">' . okj_fmt_duration(isset($entry['duration_seconds']) ? $entry['duration_seconds'] : 0)
      . okj_key_change(isset($entry['key_change']) ? $entry['key_change'] : 0)
      . okj_media_state($entry) . '</span>';
    okj_own_song_controls($entry, $i === 0, $i === $last, $canMove);
    echo '</li>';
  }
  echo '</ol></section>';
}

// The rotation proper: read-only, every singer's songs, and the one place that
// shows where this singer sits relative to everyone else. Managing your own
// songs happens in okj_render_my_queue() above.
function okj_render_queue_entries($entries, $user)
{
  // turns_until/wait_seconds describe the *singer's* next turn, so every song a
  // singer has queued carries the same pair. Showing it once, on their first
  // row, is the only reading that isn't a lie about the songs below it.
  $seenSingers = array();

  echo '<ol class="queue-list">';
  foreach ($entries as $entry)
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
      . okj_key_change(isset($entry['key_change']) ? $entry['key_change'] : 0)
      . okj_media_state($entry) . '</span>';
    // Absent on OpenKJ builds older than the ETA fields; the row just omits it.
    if ($firstForSinger && array_key_exists('turns_until', $entry))
    {
      $wait = isset($entry['wait_seconds']) ? (int)$entry['wait_seconds'] : 0;
      echo '<span class="eta">' . h(okj_fmt_turns($entry['turns_until']))
        . ($wait > 0 ? ' &middot; ' . h(okj_fmt_wait($wait)) : '') . '</span>';
    }
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
// subscriber in okj_event_stream_script() fires, and a timed poll as the
// fallback.
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
  // The pending queue, ordered by rotation position then queue position - so the
  // head of it is what "up next" means. OpenKJ used to send a second copy of this
  // array as `up_next`, which was byte-for-byte the same list and the largest
  // thing in every snapshot; reading `requests` works against those builds too.
  $requests = (isset($data['requests']) && is_array($data['requests'])) ? $data['requests'] : array();
  $recent = (isset($data['recently_played']) && is_array($data['recently_played'])) ? $data['recently_played'] : array();

  if (!empty($data['queueLocked']))
    echo '<p class="notice">Requests are closed right now.</p>';

  okj_your_turn_notice($requests, $user);

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
    if (okj_entry_is_mine($nowPlaying, $user))
    {
      // key_change is absent on OpenKJ builds without the key route, and
      // offering a control that can only 404 is worse than offering none.
      if (array_key_exists('key_change', $nowPlaying))
        okj_key_controls($nowPlaying['key_change']);
      okj_skip_control($nowPlaying);
    }
    // Last in the box, so the singer's own controls stay where they were and
    // the tally sits under them. Not gated on is_playing: OpenKJ banks a cheer
    // that lands in the gap after a song ends against the song that just
    // finished, which is exactly where the applause was aimed.
    okj_cheer_bar($nowPlaying, $user);
    echo '</section>';
  }

  okj_render_my_queue($requests, $user);

  echo '<section class="up-next"><h2>Up next</h2>';
  if (count($requests))
  {
    okj_render_queue_entries($requests, $user);
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
