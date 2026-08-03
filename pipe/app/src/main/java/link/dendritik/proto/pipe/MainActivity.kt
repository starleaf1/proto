package link.dendritik.proto.pipe

import android.Manifest
import android.companion.AssociationRequest
import android.companion.BluetoothDeviceFilter
import android.companion.CompanionDeviceManager
import android.content.IntentSender
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import link.dendritik.proto.pipe.ui.theme.ProtoPipeTheme

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
                        onGrant = ::ask,
                        onPair = ::pairWatch,
                        onUnpair = ::unpairWatch,
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
    onGrant: () -> Unit,
    onPair: () -> Unit,
    onUnpair: () -> Unit,
) {
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
                                "nearby."
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
                StatusRow("Engine running", PipeStatus.running)
                StatusRow("Watch connected", PipeStatus.watchConnected)
                if (PipeStatus.host == Host.COMPANION) {
                    StatusRow("Watch present", PipeStatus.watchPresent)
                }
                Text("Entries in window: ${PipeStatus.eventCount}")
            }
        }

        Text(
            "Whole-day entries are ignored — they have no position on the dial. " +
                "An entry without a duration is drawn as a reminder.",
            style = MaterialTheme.typography.bodySmall,
        )
    }
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
