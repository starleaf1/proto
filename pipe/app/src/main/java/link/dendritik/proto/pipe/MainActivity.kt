package link.dendritik.proto.pipe

import android.content.Intent
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.core.app.NotificationManagerCompat
import com.getpebble.android.kit.PebbleKit
import link.dendritik.proto.pipe.config.IconCategory
import link.dendritik.proto.pipe.config.PipeConfig
import link.dendritik.proto.pipe.protocol.IconState
import link.dendritik.proto.pipe.protocol.PhoneState
import link.dendritik.proto.pipe.ui.theme.ProtoPipeTheme

/**
 * Diagnostics, plus the one thing the user must do by hand: grant notification
 * access. The bridge itself is [link.dendritik.proto.pipe.notify.ProtoNotificationListener] —
 * this screen never sends anything to the watch.
 */
class MainActivity : ComponentActivity() {

    // Both can change while we are backgrounded — the user may grant access in
    // Settings, the watch may connect — so they are re-read in onResume rather than
    // observed. Cheap reads, and it keeps the screen free of a lifecycle dependency.
    private var accessGranted by mutableStateOf(false)
    private var watchConnected by mutableStateOf(false)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            ProtoPipeTheme {
                Scaffold(modifier = Modifier.fillMaxSize()) { padding ->
                    StatusScreen(
                        accessGranted = accessGranted,
                        watchConnected = watchConnected,
                        listenerBound = PipeStatus.listenerConnected,
                        state = PipeStatus.state,
                        modifier = Modifier
                            .padding(padding)
                            .padding(16.dp),
                        onGrantAccess = {
                            startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
                        },
                    )
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        accessGranted = NotificationManagerCompat
            .getEnabledListenerPackages(this)
            .contains(packageName)
        watchConnected = runCatching { PebbleKit.isWatchConnected(this) }.getOrDefault(false)
    }
}

@Composable
private fun StatusScreen(
    accessGranted: Boolean,
    watchConnected: Boolean,
    listenerBound: Boolean,
    state: IconState,
    modifier: Modifier = Modifier,
    onGrantAccess: () -> Unit,
) {
    Column(modifier = modifier.verticalScroll(rememberScrollState())) {
        Text("ProtoPipe", style = MaterialTheme.typography.headlineMedium)
        Text(
            "Bridges phone notifications to the proto watchface.",
            style = MaterialTheme.typography.bodyMedium,
        )
        Spacer(Modifier.height(20.dp))

        SectionCard("Connections") {
            StatusRow("Notification access", if (accessGranted) "Granted" else "Not granted")
            StatusRow("Listener bound", yesNo(listenerBound))
            StatusRow("Watch connected", yesNo(watchConnected))
        }

        if (!accessGranted) {
            Spacer(Modifier.height(12.dp))
            Text(
                "ProtoPipe cannot read notifications until you grant it notification " +
                    "access. Notification text stays on this device — only three counters " +
                    "are ever sent to the watch.",
                style = MaterialTheme.typography.bodySmall,
            )
            Spacer(Modifier.height(8.dp))
            Button(onClick = onGrantAccess) { Text("Open notification access settings") }
        }

        Spacer(Modifier.height(20.dp))
        SectionCard("Sent to watch") {
            StatusRow(
                "Envelope",
                if (state.unreadCount > 0) "Lit (${state.unreadCount})" else "Faded",
            )
            StatusRow("Phone icon", state.phone.describe())
            StatusRow("Missed calls", state.missedCount.toString())
        }

        Spacer(Modifier.height(20.dp))
        SectionCard("Routing") {
            Text(
                "Silent notifications are ignored, except a call already in progress.",
                style = MaterialTheme.typography.bodySmall,
            )
            Spacer(Modifier.height(8.dp))
            PipeConfig.DEFAULT.rules.forEach { rule ->
                val icons = buildList {
                    if (rule.chatChannels.isNotEmpty() || rule.fallback == IconCategory.CHAT) {
                        add("envelope")
                    }
                    if (rule.callChannels.isNotEmpty() || rule.fallback == IconCategory.CALL) {
                        add("phone")
                    }
                }
                StatusRow(rule.label, icons.joinToString(" + "))
            }
        }
        Spacer(Modifier.height(24.dp))
    }
}

private fun yesNo(value: Boolean) = if (value) "Yes" else "No"

private fun PhoneState.describe() = when (this) {
    PhoneState.IDLE -> "Faded (idle)"
    PhoneState.ONGOING -> "Green (call in progress)"
    PhoneState.RINGING -> "Flashing (ringing)"
    PhoneState.MISSED -> "Red (missed call)"
}

@Composable
private fun SectionCard(title: String, content: @Composable () -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(10.dp))
            content()
        }
    }
}

@Composable
private fun StatusRow(label: String, value: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 3.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, style = MaterialTheme.typography.bodyMedium)
        Text(value, style = MaterialTheme.typography.bodyMedium)
    }
}
