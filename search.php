<?php
include('global.inc');

global $searchLimit;

// Reduce multiple spaces to single spaces, and trim start & end whitespace
$input_query = trim(preg_replace('!\s+!', ' ', isset($_GET['q']) ? $_GET['q'] : ''));

// If query is empty, remove search.php?q= from location bar
if ($input_query == "")
{
  if (array_key_exists("HX-Request", getallheaders()))
  {
    header("HX-Replace-Url: /");
  }
  else
  {
    header("Location: /");
  }
}

$fragment = okj_is_fragment();

if (!$fragment)
{
  siteheader();
  navbar('songbook');
  searchform($input_query);
}

echo '<div id="data-target">';

// Validate that query is not just whitespace, and is 3 or more characters
if (ctype_space($input_query) || strlen($input_query) < 3) {
  helpHints();
} else {

  // OpenKJ does the matching, the artist<>DELETED / guide-vocal exclusions and
  // the ordering; it caps the result set, so the page can't blow up on a big
  // library the way an unbounded LIKE scan could.
  $res = okj_get('/local/songs', array('q' => $input_query, 'limit' => $searchLimit));

  if (!$res['ok'] && $res['status'] === 0) {
    offlineNotice($res['error']);
  } elseif (!$res['ok']) {
    errorBanner($res['error']);
  } else {
    $songs = isset($res['data']['songs']) && is_array($res['data']['songs']) ? $res['data']['songs'] : array();

    // A library can hold the same song several times over (multiple discs or
    // file formats); singers only care about one of them.
    $unique = array();
    foreach ($songs as $song)
    {
      $key = strtolower((isset($song['artist']) ? $song['artist'] : '') . ' - ' . (isset($song['title']) ? $song['title'] : ''));
      if (!isset($unique[$key])) $unique[$key] = $song;
    }

    $accepting = okj_accepting();
    $count = count($unique);
    $results_str = ($count === 1) ? 'result' : 'results';
    $truncated = (count($songs) >= $searchLimit);

    echo "<p><strong>$count search $results_str for \"" . h($input_query) . "\"</strong>";

    if ($count > 0) {
      if ($truncated) echo "<br/>Showing the first $count &mdash; narrow your search to see more.";
      if ($accepting) {
        echo '<br/>Tap a song to request it.</p><div>';
      } else {
        echo '</p><div class="not-accepting">';
      }
      songButtons($unique, $accepting);
      echo '</div>';
    } else {
      echo "</p><p>Sorry, no match found.</p>";
    }
  }
}

echo '</div>';

if (!$fragment) sitefooter();

?>
