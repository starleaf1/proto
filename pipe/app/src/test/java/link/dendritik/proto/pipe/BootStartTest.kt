package link.dendritik.proto.pipe

import android.content.Intent
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * When a reboot may start the fallback host.
 *
 * Alongside `PipeHostTest` for the same reason it exists: these are the pure decisions
 * about hosting, and every one of them fails silently on a device nobody is watching.
 *
 * `Intent.ACTION_BOOT_COMPLETED` and `ACTION_MY_PACKAGE_REPLACED` are plain string
 * constants, so this needs no framework and no Robolectric.
 */
class BootStartTest {

    private val boot = Intent.ACTION_BOOT_COMPLETED
    private val replaced = Intent.ACTION_MY_PACKAGE_REPLACED

    @Test
    fun `below the cap a reboot may start the fallback host`() {
        assertTrue(canStartFallbackFrom(24, boot))
        assertTrue(canStartFallbackFrom(FGS_TIMEOUT_SDK - 1, boot))
    }

    @Test
    fun `from the cap onward a reboot may not`() {
        assertFalse(canStartFallbackFrom(FGS_TIMEOUT_SDK, boot))
        assertFalse(canStartFallbackFrom(FGS_TIMEOUT_SDK + 1, boot))
    }

    /** The rule names BOOT_COMPLETED alone, which is why an update still restores it. */
    @Test
    fun `an app update may start it on every release`() {
        assertTrue(canStartFallbackFrom(24, replaced))
        assertTrue(canStartFallbackFrom(FGS_TIMEOUT_SDK, replaced))
        assertTrue(canStartFallbackFrom(FGS_TIMEOUT_SDK + 10, replaced))
    }

    @Test
    fun `an unexpected action is not treated as a boot`() {
        assertTrue(canStartFallbackFrom(FGS_TIMEOUT_SDK, null))
        assertTrue(canStartFallbackFrom(FGS_TIMEOUT_SDK, "com.example.WHATEVER"))
    }
}
