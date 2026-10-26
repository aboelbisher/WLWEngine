package com.wlw.kit

import android.app.Activity
import android.util.Log
import com.google.android.gms.games.AuthenticationResult
import com.google.android.gms.games.PlayGames
import com.google.android.gms.games.PlayGamesSdk
import com.google.android.gms.games.leaderboard.LeaderboardVariant
import com.google.android.gms.tasks.Task
import com.wlw.WlwBridge
import com.wlw.WlwService
import org.json.JSONArray
import org.json.JSONObject

// The "leaderboards" service of the native bridge: Google Play Games Services v2 sign-in and
// leaderboards, for wlw::kit::Leaderboards (kit/wlw/kit/leaderboards.h). Runs on the UI thread.
//
// Calls from C++:  init, sign_in, submit {board, score}, load {id, board, span, count}
// Events to C++:   state {signed_in}, loaded {id, ok, top: [{rank, score, name, me}], player, players},
//                  error {method, message}
//
// A game using it adds this folder to its sourceSets, depends on
// com.google.android.gms:play-services-games-v2, and names its Play Games project in the manifest:
//   <meta-data android:name="com.google.android.gms.games.APP_ID" android:value="@string/game_services_project_id"/>
// (the string comes from Play Console's "Get resources"). Sign-in is shared by the whole app, so a
// later cloud-save service can use it too.
class PlayGamesService(private val activity: Activity) : WlwService {
    override val name = "leaderboards"

    private var playerId: String? = null  // the signed-in player: marks their own row
    private var playerName = ""
    private val stale = mutableSetOf<String>()  // boards submitted to since their last load: skip the cache
    private var destroyed = false

    init {
        // Before any other Play Games call; the SDK then signs the player in silently.
        PlayGamesSdk.initialize(activity.applicationContext)
    }

    override fun onCall(method: String, args: JSONObject) {
        when (method) {
            "init" -> PlayGames.getGamesSignInClient(activity).isAuthenticated().addOnCompleteListener { onSignIn(it) }
            "sign_in" -> PlayGames.getGamesSignInClient(activity).signIn().addOnCompleteListener { onSignIn(it) }
            "submit" -> submit(args.optString("board"), args.optLong("score"))
            "load" -> load(args)
            else -> reportError(method, "unknown method")
        }
    }

    override fun onDestroy() {
        destroyed = true
    }

    private fun onSignIn(task: Task<AuthenticationResult>) {
        if (destroyed) return
        if (!task.isSuccessful || task.result?.isAuthenticated != true) {
            task.exception?.let { Log.w(TAG, "Play Games sign-in: ${it.message}") }
            playerId = null
            playerName = ""
            sendState(false)
            return
        }
        PlayGames.getPlayersClient(activity).currentPlayer.addOnCompleteListener { playerTask ->
            val player = if (playerTask.isSuccessful) playerTask.result else null
            playerId = player?.playerId
            playerName = player?.displayName ?: ""
            if (!destroyed) sendState(true)
        }
    }

    private fun submit(board: String, score: Long) {
        if (board.isEmpty()) return reportError("submit", "no board")
        // Queued by Play Games and sent when online; a lower score than the best changes nothing.
        PlayGames.getLeaderboardsClient(activity).submitScore(board, score)
        stale.add(board)
    }

    private fun load(args: JSONObject) {
        val id = args.optInt("id")
        val board = args.optString("board")
        val span = when (args.optString("span")) {
            "today" -> LeaderboardVariant.TIME_SPAN_DAILY
            "week" -> LeaderboardVariant.TIME_SPAN_WEEKLY
            else -> LeaderboardVariant.TIME_SPAN_ALL_TIME
        }
        val count = args.optInt("count", 10).coerceIn(1, 25)
        val collection = LeaderboardVariant.COLLECTION_PUBLIC
        val fresh = stale.remove(board)  // just submitted: the cached table may not have it yet
        PlayGames.getLeaderboardsClient(activity)
            .loadTopScores(board, span, collection, count, fresh)
            .addOnCompleteListener { task ->
                val scores = if (task.isSuccessful) task.result?.get() else null
                if (scores == null) {
                    task.exception?.let { Log.w(TAG, "Leaderboard $board: ${it.message}") }
                    if (fresh) stale.add(board)
                    WlwBridge.post(name, "loaded", JSONObject().put("id", id).put("ok", false))
                    return@addOnCompleteListener
                }
                try {
                    val top = JSONArray()
                    for (score in scores.scores) {
                        top.put(JSONObject()
                            .put("rank", score.rank)
                            .put("score", score.rawScore)
                            .put("name", score.scoreHolderDisplayName)
                            .put("me", playerId != null && score.scoreHolder?.playerId == playerId))
                    }
                    val result = JSONObject().put("id", id).put("ok", true).put("top", top)
                    // The leaderboard's variant for this span holds the player's own rank and the
                    // number of players, even when the player isn't in the top.
                    val variant = scores.leaderboard?.variants?.firstOrNull {
                        it.timeSpan == span && it.collection == collection
                    }
                    if (variant != null) {
                        result.put("players", variant.numScores)
                        if (variant.hasPlayerInfo() && variant.playerRank != LeaderboardVariant.PLAYER_RANK_UNKNOWN.toLong()) {
                            result.put("player", JSONObject()
                                .put("rank", variant.playerRank)
                                .put("score", variant.rawPlayerScore)
                                .put("name", playerName)
                                .put("me", true))
                        }
                    }
                    WlwBridge.post(name, "loaded", result)
                } finally {
                    scores.release()
                }
            }
    }

    private fun sendState(signedIn: Boolean) {
        WlwBridge.post(name, "state", JSONObject().put("signed_in", signedIn))
    }

    private fun reportError(method: String, message: String) {
        Log.w(TAG, "Leaderboards: $method: $message")
        WlwBridge.post(name, "error", JSONObject().put("method", method).put("message", message))
    }

    private companion object {
        const val TAG = "WLW"
    }
}
