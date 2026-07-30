package link.dendritik.proto.pipe

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
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
 * Diagnostics, and the two grants the service cannot get for itself.
 *
 * Calendar access is a runtime permission and notification access is needed for the
 * foreground service's own notification, so unlike the previous design there is
 * actual permission machinery here rather than a single jump to a settings screen.
 */
class MainActivity : ComponentActivity() {

    private var calendarGranted by mutableStateOf(false)
    private var notificationsGranted by mutableStateOf(true)

    private val requestPermissions = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
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
                        onGrant = ::ask,
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
        notificationsGranted = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            granted(Manifest.permission.POST_NOTIFICATIONS)
        } else {
            true
        }
        PipeStatus.calendarGranted = calendarGranted
    }

    private fun granted(permission: String) =
        ContextCompat.checkSelfPermission(this, permission) == PackageManager.PERMISSION_GRANTED

    private fun ask() {
        val wanted = buildList {
            if (!calendarGranted) add(Manifest.permission.READ_CALENDAR)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU && !notificationsGranted) {
                add(Manifest.permission.POST_NOTIFICATIONS)
            }
        }
        if (wanted.isNotEmpty()) requestPermissions.launch(wanted.toTypedArray())
    }

    /**
     * Starting is idempotent, so this runs on every resume rather than only once —
     * that is what recovers the service after the system has killed it.
     */
    private fun maybeStart() {
        if (calendarGranted) PipeService.start(this)
    }
}

@Composable
private fun StatusScreen(
    modifier: Modifier = Modifier,
    calendarGranted: Boolean,
    notificationsGranted: Boolean,
    onGrant: () -> Unit,
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

        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Status", style = MaterialTheme.typography.titleMedium)
                StatusRow("Service running", PipeStatus.running)
                StatusRow("Watch connected", PipeStatus.watchConnected)
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
