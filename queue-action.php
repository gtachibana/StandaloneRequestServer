<?php
include('global.inc');
include('queue-render.inc');

// Every action here is scoped to the signed-in user's own songs; the API
// re-checks ownership, this is just to avoid pointless round trips.
$user = okj_user();
$action = isset($_POST['action']) ? $_POST['action'] : '';
$entryId = isset($_POST['entryId']) ? (string)$_POST['entryId'] : '';
$error = '';

if (!$user['authenticated'])
{
  $error = 'Please sign in first.';
}
elseif ($action === 'away')
{
  $away = (isset($_POST['away']) && $_POST['away'] === '1');
  $res = okj_post('/local/user/away', array('token' => okj_token(), 'away' => $away));
  if (!$res['ok']) $error = $res['error'];
}
elseif ($action === 'remove')
{
  $res = okj_post('/local/request/remove', array('token' => okj_token(), 'entryId' => $entryId));
  if (!$res['ok']) $error = $res['error'];
}
elseif ($action === 'up' || $action === 'down')
{
  $res = okj_post('/local/request/move', array(
    'token' => okj_token(),
    'entryId' => $entryId,
    'direction' => $action,
  ));
  if (!$res['ok']) $error = $res['error'];
}
elseif ($action === 'skip')
{
  // Carries no entryId: the song being sung right now is the API's own to
  // identify, and it is also the only thing that can say whether this token is
  // the one holding the mic. Nothing is written down on success - the queue row
  // is already played and the history row went in when the song started, so a
  // song bailed out of halfway still counts as this singer's turn.
  $res = okj_post('/local/request/skip', array('token' => okj_token()));
  if (!$res['ok']) $error = $res['error'];
}
else
{
  $error = 'Unknown action.';
}

// okj_user() memoized the pre-action state; the away flag it holds is now stale.
if ($action === 'away' && $error === '') okj_user_refresh();

renderQueueFragment($error);

?>
