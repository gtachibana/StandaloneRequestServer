<?php
include('global.inc');

global $historyLimit;

// One row per distinct song the singer has sung: OpenKJ bumps a play count and
// lastplay rather than appending a row per performance, so this reads as "the
// songs I sing" more than as a log - which is why every row still in the library
// is a request button.
//
// Two things about the source shape what this page can say. OpenKJ only records
// a song when the singer is a regular (or the KJ has "treat all singers as
// regulars" on), so an empty list is normal rather than an error; and rows are
// keyed by rotation singer name, which for a local user is their username, so
// this only covers nights they sang signed in under this account.

// last_played is OpenKJ's local wall-clock time with no UTC offset (Qt's
// ISODate on a local QDateTime), so it only means anything once PHP is standing
// in the same zone - which is what $venueTimezone is for. Day granularity is
// deliberate even so: a venue that hasn't set that setting is silently in UTC,
// and "last Friday" survives an offset that "11:58pm" would not.
function okj_fmt_last_sung($value)
{
  $when = strtotime((string)$value);
  if ($when === false) return '';

  $today = strtotime('today');
  if ($when >= $today) return 'today';
  if ($when >= strtotime('yesterday')) return 'yesterday';
  // Inside a week the weekday alone is the most useful form; past that it stops
  // being unambiguous and the date has to do the work.
  if ($when >= strtotime('-6 days', $today)) return date('l', $when);
  if (date('Y', $when) === date('Y')) return date('M j', $when);
  return date('M Y', $when);
}

$fragment = okj_is_fragment();
$user = okj_user();

if (!$fragment)
{
  siteheader();
  navbar('history');
  echo '<h2 class="page-title">Songs you&rsquo;ve sung</h2>';
}

echo '<div id="history-target">';

if (!$user['authenticated'])
{
  echo '<p>Your history is tied to an account. <a href="/account.php" hx-boost="true">Sign in</a>
    to see what you&rsquo;ve sung here before.</p>';
}
else
{
  $res = okj_get('/local/user/history', array('token' => okj_token(), 'limit' => $historyLimit));

  if (!$res['ok'] && $res['status'] === 0)
  {
    offlineNotice($res['error']);
  }
  // An OpenKJ predating the history route 404s with "Not Found", which is not
  // something to hand a singer as an error banner.
  elseif (!$res['ok'] && $res['status'] === 404)
  {
    echo '<p>This venue&rsquo;s karaoke software doesn&rsquo;t keep a singer history yet.</p>';
  }
  elseif (!$res['ok'])
  {
    errorBanner($res['error']);
  }
  else
  {
    $songs = (isset($res['data']['songs']) && is_array($res['data']['songs'])) ? $res['data']['songs'] : array();

    if (!count($songs))
    {
      echo '<p>Nothing here yet.</p>
        <p class="hint">Songs show up here after you&rsquo;ve sung them. Find one in
        <a href="/" hx-boost="true">search</a> or
        <a href="/browse.php" hx-boost="true">browse</a> to get started.</p>';
    }
    else
    {
      $accepting = okj_accepting();
      // Session-cached, so pairing every row with a star costs nothing extra
      // once the singer has loaded any result list this session.
      $favorites = okj_favorite_ids();
      $count = count($songs);

      echo '<p><strong>' . $count . ' ' . ($count === 1 ? 'song' : 'songs') . '</strong>';
      echo $accepting
        ? '<br/>Tap one to sing it again.</p>'
        : '</p><p class="notice">Requests are closed right now.</p>';

      // The API sorts by lastplay DESC, so a full page is the most recent N.
      if ($count >= $historyLimit)
        echo '<p class="hint">Showing your ' . $historyLimit . ' most recent.</p>';

      echo '<div' . ($accepting ? '' : ' class="not-accepting"') . '>';
      foreach ($songs as $song)
      {
        $songId = isset($song['song_id']) ? (int)$song['song_id'] : 0;
        $artist = isset($song['artist']) ? $song['artist'] : '';
        $title = isset($song['title']) ? $song['title'] : '';
        $label = h($artist . ' - ' . $title);
        // `available` is the API's join to dbsongs having found the file. A song
        // the library has since lost still belongs in the list, but there is
        // nothing to request.
        $inLibrary = ($songId > 0 && !empty($song['available']));

        echo '<div class="history-entry">';
        echo '<div class="result-row">';

        if ($accepting && $inLibrary)
        {
          $vals = hxvals(array('songid' => $songId, 'artist' => $artist, 'title' => $title));
          echo "<button
            class=\"result song\"
            hx-post=\"/req-modal.php\"
            hx-target=\"body\"
            hx-swap=\"beforeend\"
            hx-vals='$vals'>$label</button>";
        }
        else
        {
          echo "<button class=\"result song\">$label</button>";
        }

        // Default toggle mode: the star swaps only itself, so promoting a past
        // song to a favorite leaves the list where it is.
        if ($inLibrary) favStar($songId, isset($favorites[$songId]));
        echo '</div>';

        $meta = array();
        $lastSung = okj_fmt_last_sung(isset($song['last_played']) ? $song['last_played'] : '');
        if ($lastSung !== '') $meta[] = 'Last sung ' . $lastSung;
        $plays = isset($song['plays']) ? (int)$song['plays'] : 0;
        if ($plays > 1) $meta[] = $plays . ' times';
        if (!$inLibrary) $meta[] = 'no longer in the songbook';
        // Every part is generated here - weekday names, integers and literals -
        // so there is nothing from the API left to escape.
        if (count($meta)) echo '<p class="hint song-meta">' . implode(' &middot; ', $meta) . '</p>';

        echo '</div>';
      }
      echo '</div>';
    }
  }
}

echo '</div>';

if (!$fragment) sitefooter();

?>
