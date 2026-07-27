package link.dendritik.proto.pipe.notify

import android.app.Notification
import android.content.Context
import android.os.Build
import android.provider.Telephony
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.telecom.TelecomManager
import android.util.Log
import link.dendritik.proto.pipe.PipeStatus
import link.dendritik.proto.pipe.config.DynamicPackage
import link.dendritik.proto.pipe.config.PipeConfig
import link.dendritik.proto.pipe.pebble.PebbleSender

/**
 * The app's only data source. The system binds this once the user grants
 * notification access; nothing here starts it.
 *
 * Its whole job is to keep [ActiveSet] in step with the shade and hand each new
 * snapshot to [PebbleSender]. All routing decisions live in [Classifier], which
 * this class feeds with plain [NotificationFacts] — notification *content* (titles,
 * text, senders) is read only to spot answer/decline buttons and never stored,
 * logged, or sent to the watch.
 */
class ProtoNotificationListener : NotificationListenerService() {

    private val active = ActiveSet()
    private lateinit var sender: PebbleSender
    private var config = PipeConfig.DEFAULT

    override fun onCreate() {
        super.onCreate()
        sender = PebbleSender(applicationContext)
    }

    /**
     * Called on connect and after the system rebinds us. The shade is the source of
     * truth, so rebuild the whole set rather than trusting anything we held before.
     */
    override fun onListenerConnected() {
        super.onListenerConnected()
        config = PipeConfig.DEFAULT.resolve(::resolvePackage)
        sender.start()
        rebuild()
        PipeStatus.listenerConnected = true
        Log.i(TAG, "listener connected")
    }

    override fun onListenerDisconnected() {
        PipeStatus.listenerConnected = false
        sender.stop()
        super.onListenerDisconnected()
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        val verdict = Classifier.classify(factsOf(sbn), config)
        if (active.put(sbn.key, verdict)) publish()
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        // The dismissal path: when the last chat or call notification leaves, the
        // set empties and the snapshot returns the icon to its faded state.
        if (active.remove(sbn.key)) publish()
    }

    /**
     * A ranking change can flip a notification's importance, which decides whether
     * we forward it at all — so re-classify everything rather than ignore it.
     */
    override fun onNotificationRankingUpdate(update: RankingMap?) {
        rebuild()
    }

    private fun rebuild() {
        val current = runCatching { activeNotifications }.getOrNull() ?: return
        active.reset(current.associate { it.key to Classifier.classify(factsOf(it), config) })
        publish()
    }

    private fun publish() {
        val snapshot = active.snapshot()
        PipeStatus.state = snapshot
        sender.submit(snapshot)
    }

    /** Flatten a live notification into the pure data the classifier works on. */
    private fun factsOf(sbn: StatusBarNotification): NotificationFacts {
        val n = sbn.notification
        val extras = n.extras
        return NotificationFacts(
            key = sbn.key,
            packageName = sbn.packageName,
            channelId = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) n.channelId else null,
            category = n.category,
            importance = importanceOf(sbn),
            priority = n.priority,
            isOngoing = n.flags and Notification.FLAG_ONGOING_EVENT != 0,
            isClearable = sbn.isClearable,
            isGroupSummary = n.flags and Notification.FLAG_GROUP_SUMMARY != 0,
            hasFullScreenIntent = n.fullScreenIntent != null,
            actionTitles = n.actions?.mapNotNull { it.title?.toString()?.lowercase() } ?: emptyList(),
            callType = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
                extras.containsKey(Notification.EXTRA_CALL_TYPE)
            ) {
                extras.getInt(Notification.EXTRA_CALL_TYPE)
            } else {
                null
            },
        )
    }

    /**
     * Channel importance, which is how "the user silenced this" is expressed from
     * API 26 on. Null below that, where [Classifier] falls back to `priority`.
     */
    private fun importanceOf(sbn: StatusBarNotification): Int? {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return null
        val ranking = NotificationListenerService.Ranking()
        return if (currentRanking?.getRanking(sbn.key, ranking) == true) ranking.importance else null
    }

    /** The user's chosen SMS and dialler apps, which are per-device. */
    private fun resolvePackage(which: DynamicPackage): String? = runCatching {
        when (which) {
            DynamicPackage.DEFAULT_SMS -> Telephony.Sms.getDefaultSmsPackage(this)
            DynamicPackage.DEFAULT_DIALER ->
                getSystemService(Context.TELECOM_SERVICE)
                    ?.let { (it as TelecomManager).defaultDialerPackage }
        }
    }.getOrNull()

    private companion object {
        const val TAG = "PipeListener"
    }
}
