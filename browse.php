<?php
include('global.inc');

global $browseLimit;

$by = (isset($_GET['by']) && $_GET['by'] === 'title') ? 'title' : 'artist';
$letter = strtoupper(trim(isset($_GET['letter']) ? $_GET['letter'] : 'A'));
if (!preg_match('/^[A-Z]$/', $letter)) $letter = 'A';

$fragment = okj_is_fragment();

if (!$fragment)
{
  siteheader();
  navbar('browse');
  searchform();
}

echo '<div id="data-target">';

echo '<div class="browse-controls" hx-target="#data-target" hx-select="#data-target" hx-push-url="true">';

echo '<div class="browse-by">Browse by ';
foreach (array('artist' => 'Artist', 'title' => 'Title') as $value => $label)
{
  $class = ($by === $value) ? ' class="current"' : '';
  echo "<button$class hx-get=\"/browse.php?by=$value&letter=" . h($letter) . "\">" . h($label) . "</button> ";
}
echo '</div>';

echo '<div class="browse-letters">';
foreach (range('A', 'Z') as $l)
{
  $class = ($l === $letter) ? ' class="current"' : '';
  echo "<button$class hx-get=\"/browse.php?by=" . h($by) . "&letter=$l\">$l</button>";
}
echo '</div></div>';

$res = okj_post('/browse', array('by' => $by, 'letter' => $letter, 'limit' => $browseLimit));

if (!$res['ok'] && $res['status'] === 0) {
  offlineNotice($res['error']);
} elseif (!$res['ok']) {
  errorBanner($res['error']);
} else {
  $songs = isset($res['data']['songs']) && is_array($res['data']['songs']) ? $res['data']['songs'] : array();

  $unique = array();
  foreach ($songs as $song)
  {
    $key = strtolower((isset($song['artist']) ? $song['artist'] : '') . ' - ' . (isset($song['title']) ? $song['title'] : ''));
    if (!isset($unique[$key])) $unique[$key] = $song;
  }

  $accepting = okj_accepting();
  $count = count($unique);
  $noun = ($by === 'title') ? 'titles' : 'artists';

  echo "<p><strong>$count $noun starting with &ldquo;" . h($letter) . "&rdquo;</strong>";
  if (count($songs) >= $browseLimit) echo "<br/>Showing the first $count &mdash; search to find something specific.";

  if ($count > 0) {
    if ($accepting) {
      echo '<br/>Tap a song to request it.</p><div>';
    } else {
      echo '</p><div class="not-accepting">';
    }
    songButtons($unique, $accepting);
    echo '</div>';
  } else {
    echo '</p><p>Nothing in the songbook under that letter.</p>';
  }
  echo '<p class="hint">Browsing only covers A&ndash;Z. Use search for songs starting with a number or symbol.</p>';
}

echo '</div>';

if (!$fragment) sitefooter();

?>
