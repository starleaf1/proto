package link.dendritik.proto.pipe

import android.Manifest
import android.companion.AssociationRequest
import android.companion.BluetoothDeviceFilter
import android.companion.CompanionDeviceManager
import android.content.Context
import android.content.IntentSender
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.text.format.DateFormat
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import link.dendritik.proto.pipe.pebble.PebbleSender
import link.dendritik.proto.pipe.ui.theme.ProtoPipeTheme
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.util.Date

/**
 * Diagnostics, and the three grants the hosts cannot get for themselves.
 *
 * Calendar access is a runtime permission. Notification access is needed only by the
 * foreground-service host, so it is asked for only when that is the host we are heading
 * for. Associating the watch is neither — it is a system dialog, and it is what buys the
 * host that has no notification at all.
 */
class MainActivity : ComponentActivity() {

    private var calendarGranted by mutableStateOf(false)
    private var notificationsGranted by mutableStateOf(true)
    private var associated by mutableStateOf(false)

    private val requestPermissions = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { refresh(); maybeStart() }

    /** The system's device-picker. Its result is the association itself. */
    private val associate = registerForActivityResult(
        ActivityResultContracts.StartIntentSenderForResult(),
    ) { refresh(); maybeStart() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            ProtoPipeTheme {
                Scaffold(modifier = Modifier.fillMaxSize()) { insets ->
                    StatusScreen(
                        modifier = Modifier.padding(insets),
                        calendarGranted = calendarGranted,
                        notificationsGranted = notificationsGranted,
                        associated = associated,
                        canAssociate = Build.VERSION.SDK_INT >= MIN_COMPANION_SDK,
                        fallbackCapped = Build.VERSION.SDK_INT >= FGS_TIMEOUT_SDK,
                        onGrant = ::ask,
                        onPair = ::pairWatch,
                        onUnpair = ::unpairWatch,
                        onResync = ::resyncNow,
                    )
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        refresh()
        maybeStart()
    }

    private fun refresh() {
        calendarGranted = granted(Manifest.permission.READ_CALENDAR)
        associated = CompanionHost.associations(this).isNotEmpty()
        notificationsGranted = when {
            Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU -> true
            // The companion host posts nothing, so a missing grant is not a gap.
            hostAhead() == Host.COMPANION -> true
            else -> granted(Manifest.permission.POST_NOTIFICATIONS)
        }
        PipeStatus.calendarGranted = calendarGranted
        PipeStatus.host = hostAhead()
        // The engine's reconcile() is the only other writer of this, so on screen it can
        // be a whole tick period out of date — and it is what gates the re-sync button.
        PipeStatus.watchConnected = PebbleSender.watchConnected(this)
        // Last press's verdict does not outlive the visit that produced it.
        PipeStatus.resync = Resync.IDLE
    }

    private fun hostAhead(): Host = chooseHost(Build.VERSION.SDK_INT, associated)

    private fun granted(permission: String) =
        ContextCompat.checkSelfPermission(this, permission) == PackageManager.PERMISSION_GRANTED

    private fun ask() {
        val wanted = buildList {
            if (!calendarGranted) add(Manifest.permission.READ_CALENDAR)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
                !notificationsGranted && hostAhead() == Host.FOREGROUND
            ) {
                add(Manifest.permission.POST_NOTIFICATIONS)
            }
        }
        if (wanted.isNotEmpty()) requestPermissions.launch(wanted.toTypedArray())
    }

    /**
     * Starting is idempotent, so this runs on every resume rather than only once —
     * that is what recovers the service after the system has killed it.
     *
     * The two hosts are mutually exclusive, and this is the only place that enforces it:
     * taking the companion path stops the foreground service, which is the moment the
     * notification goes away.
     */
    private fun maybeStart() {
        if (!calendarGranted) return
        when (hostAhead()) {
            Host.COMPANION -> {
                CompanionHost.startObserving(this)
                PipeService.stop(this)
            }
            Host.FOREGROUND -> PipeService.start(this)
        }
    }

    /**
     * Ask the running engine for an immediate flush.
     *
     * Fire and forget — there is no engine reference to call and no result to await, so
     * the outcome comes back through [PipeStatus.resync], which the engine sets once it
     * has handled the broadcast. [Resync.REQUESTED] is what is left if nothing does: the
     * button is only enabled while `PipeStatus.running`, but the host can be torn down
     * between the check and the press.
     */
    private fun resyncNow() {
        PipeStatus.resync = Resync.REQUESTED
        PipeEngine.requestResync(this)
    }

    // ---------------------------------------------------------------------------
    // Association
    // ---------------------------------------------------------------------------

    /**
     * Asks the system to let the user pick their watch.
     *
     * The dialog does the Bluetooth scanning on our behalf, which is the point: no
     * Bluetooth permission of our own, and no battery-optimisation exemption to beg
     * for. What comes back is an association the platform remembers, and from then on it
     * binds [PipeCompanionService] whenever that device is nearby.
     */
    private fun pairWatch() {
        if (Build.VERSION.SDK_INT < MIN_COMPANION_SDK) return
        val cdm = getSystemService(CompanionDeviceManager::class.java) ?: return
        val request = AssociationRequest.Builder()
            .addDeviceFilter(BluetoothDeviceFilter.Builder().build())
            .setSingleDevice(false)
            .build()
        val callback = object : CompanionDeviceManager.Callback() {
            @Deprecated("Required below API 33; the AssociationInfo overload is 33+.")
            override fun onDeviceFound(chooser: IntentSender) {
                runCatching {
                    associate.launch(IntentSenderRequest.Builder(chooser).build())
                }.onFailure { Log.w(TAG, "could not show the picker", it) }
            }

            override fun onFailure(error: CharSequence?) {
                Log.w(TAG, "association failed: $error")
            }
        }
        runCatching { cdm.associate(request, callback, null) }
            .onFailure { Log.w(TAG, "could not start association", it) }
    }

    /**
     * Hands the job back to the foreground service.
     *
     * Worth keeping rather than hiding behind a debug flag: it is the way to re-run the
     * association flow while verifying it, and the honest way out if the system's
     * binding turns out not to hold on a given device.
     */
    private fun unpairWatch() {
        CompanionHost.forget(this)
        refresh()
        maybeStart()
    }

    private companion object {
        const val TAG = "MainActivity"
    }
}

@Composable
private fun StatusScreen(
    modifier: Modifier = Modifier,
    calendarGranted: Boolean,
    notificationsGranted: Boolean,
    associated: Boolean,
    canAssociate: Boolean,
    fallbackCapped: Boolean,
    onGrant: () -> Unit,
    onPair: () -> Unit,
    onUnpair: () -> Unit,
    onResync: () -> Unit,
) {
    val scope = rememberCoroutineScope()
    var spinning by remember { mutableStateOf(false) }

    Column(
        modifier = modifier.fillMaxSize().padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("proto", style = MaterialTheme.typography.headlineMedium)
        Text(
            "Sends your next six hours to the watch.",
            style = MaterialTheme.typography.bodyMedium,
        )

        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Permissions", style = MaterialTheme.typography.titleMedium)
                StatusRow("Calendar access", calendarGranted)
                StatusRow("Notifications", notificationsGranted)
            }
        }

        if (!calendarGranted || !notificationsGranted) {
            Button(onClick = onGrant, modifier = Modifier.fillMaxWidth()) {
                Text("Grant access")
            }
        }

        if (canAssociate) {
            Card(Modifier.fillMaxWidth()) {
                Column(
                    Modifier.padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Text("Watch pairing", style = MaterialTheme.typography.titleMedium)
                    Text(
                        if (associated) {
                            "Paired. Android keeps proto running while the watch is " +
                                "nearby, so there is no ongoing notification."
                        } else {
                            "Pair the watch to remove the ongoing notification. " +
                                "Android will then run proto only while the watch is " +
                                "nearby." +
                                if (fallbackCapped) {
                                    " Pairing also lifts the six-hour daily limit " +
                                        "Android puts on the unpaired host, which " +
                                        "otherwise stops proto syncing until you open " +
                                        "it again."
                                } else {
                                    ""
                                }
                        },
                        style = MaterialTheme.typography.bodySmall,
                    )
                    if (associated) {
                        TextButton(onClick = onUnpair) { Text("Unpair") }
                    } else {
                        Button(onClick = onPair, modifier = Modifier.fillMaxWidth()) {
                            Text("Pair watch")
                        }
                    }
                }
            }
        }

        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Status", style = MaterialTheme.typography.titleMedium)
                Text("Host: ${PipeStatus.host.name.lowercase()}")
                if (PipeStatus.capped) {
                    Text(
                        "Android has paused this host — it may run six hours a day. " +
                            "Opening proto resets that; pairing the watch removes it.",
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
                StatusRow("Engine running", PipeStatus.running)
                StatusRow("Watch connected", PipeStatus.watchConnected)
                if (PipeStatus.host == Host.COMPANION) {
                    StatusRow("Watch present", PipeStatus.watchPresent)
                }
                Text("Entries in window: ${PipeStatus.eventCount}")
                Text("Last sync: ${lastSyncText(LocalContext.current, PipeStatus.lastSyncAtMs)}")

                // Enabled only where a flush can land: syncCalendar returns early with
                // the link down, so a press in any other state would be a silent no-op.
                // Disabled rather than hidden — a greyed button under a "Watch
                // connected: no" row explains itself, and a missing one explains nothing.
                // The debounce is inside onClick rather than in `enabled` on purpose. A
                // disabled M3 button drops its content to 38% alpha over a 12% container,
                // which would leave the spinner and its label almost invisible — and a
                // spinner nobody can see is the thing SPINNER_MIN_MS exists to prevent.
                Button(
                    onClick = {
                        if (!spinning) {
                            spinning = true
                            scope.launch {
                                onResync()
                                delay(SPINNER_MIN_MS)
                                spinning = false
                            }
                        }
                    },
                    enabled = PipeStatus.running && PipeStatus.watchConnected,
                    modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
                ) {
                    if (spinning) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(16.dp),
                            strokeWidth = 2.dp,
                            color = LocalContentColor.current,
                        )
                        Spacer(Modifier.width(8.dp))
                    }
                    Text(if (spinning) "Re-syncing…" else "Re-sync now")
                }
                // The outcome only means something once the spinner is down; until then
                // it is either stale or the word the spinner is already saying.
                if (!spinning) {
                    resyncText(PipeStatus.resync)?.let {
                        Text(it, style = MaterialTheme.typography.bodySmall)
                    }
                }
            }
        }

        Text(
            "Whole-day entries are ignored — they have no position on the dial. " +
                "An entry without a duration is drawn as a reminder.",
            style = MaterialTheme.typography.bodySmall,
        )
    }
}

/**
 * How long the spinner stays up, at minimum.
 *
 * Not a timeout, and not an estimate of how long a flush takes. The flush runs
 * synchronously inside the broadcast's own dispatch on this very thread, so it cannot
 * animate anything while it works and it is normally over within a frame of the press —
 * a spinner tied strictly to it would be a flicker nobody could read. This is the
 * shortest press-to-feedback that reads as an action having been taken, and because the
 * work finishes inside it, what follows the delay is the outcome rather than a guess at
 * it. If the flush ever does move off the main thread, this becomes a floor rather than
 * the whole duration, and nothing here has to change.
 */
private const val SPINNER_MIN_MS = 450L

/** A time of day in the user's own format, or an em dash before the first send. */
private fun lastSyncText(context: Context, atMs: Long): String =
    if (atMs == 0L) "—" else DateFormat.getTimeFormat(context).format(Date(atMs))

private fun resyncText(state: Resync): String? = when (state) {
    Resync.IDLE -> null
    // Still REQUESTED with the spinner down means nothing ever picked the broadcast up.
    Resync.REQUESTED -> "Nothing answered — no engine is running."
    Resync.SENT -> "Flushed the window to the watch."
    Resync.NOTHING_SENT -> "Nothing went out — check the link."
}

@Composable
private fun StatusRow(label: String, ok: Boolean) {
    Row(
        Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label)
        Text(if (ok) "yes" else "no", style = MaterialTheme.typography.labelLarge)
    }
}
