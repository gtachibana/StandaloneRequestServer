<?php

// The whole swapped region of the favorites page, wrapper included, so that
// favorite-action.php can hand back a fresh copy after a removal the same way
// queue-action.php does for the rotation.
function renderFavoritesFragment($actionError = '')
{
  $user = okj_user();

  echo '<div id="favorites-target">';

  errorBanner($actionError);

  if (!$user['authenticated'])
  {
    echo '<p>Favorites are tied to an account. <a href="/account.php" hx-boost="true">Sign in</a>
      to keep a list of the songs you come back to.</p>';
    echo '</div>';
    return;
  }

  $res = okj_get('/local/user/favorites', array('token' => okj_token()));

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

  // This response is the same list okj_favorite_ids() caches, so refresh the
  // cache from it rather than making the next search page re-fetch it.
  okj_favorites_store($res['data'], strtolower($user['username']));

  $songs = (isset($res['data']['songs']) && is_array($res['data']['songs'])) ? $res['data']['songs'] : array();

  if (!count($songs))
  {
    echo '<p>No favorites yet.</p>
      <p class="hint">Tap the &#9734; next to any song in
      <a href="/" hx-boost="true">search</a> or
      <a href="/browse.php" hx-boost="true">browse</a> to save it here.</p>';
    echo '</div>';
    return;
  }

  $accepting = okj_accepting();
  $count = count($songs);

  echo '<p><strong>' . $count . ' saved ' . ($count === 1 ? 'song' : 'songs') . '</strong>';
  echo $accepting
    ? '<br/>Tap a song to request it.</p>'
    : '</p><p class="notice">Requests are closed right now.</p>';

  echo '<div' . ($accepting ? '' : ' class="not-accepting"') . '>';
  foreach ($songs as $song)
  {
    $songId = isset($song['song_id']) ? (int)$song['song_id'] : 0;
    $artist = isset($song['artist']) ? $song['artist'] : '';
    $title = isset($song['title']) ? $song['title'] : '';
    $label = h($artist . ' - ' . $title);

    echo '<div class="result-row">';
    if ($accepting && $songId > 0)
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
    // Always starred here by definition, so the star is the remove control -
    // and it re-renders the list, since the row itself has to go.
    favStar($songId, true, '', 'list');
    echo '</div>';
  }
  echo '</div>';

  echo '</div>';
}

?>
