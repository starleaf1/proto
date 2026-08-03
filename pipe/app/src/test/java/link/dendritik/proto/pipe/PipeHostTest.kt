package link.dendritik.proto.pipe

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The host decision, which is the one branch in this change with a wrong answer that
 * would be invisible: pick the companion host where the platform cannot bind it and the
 * app simply stops sending, with no notification to show that it has.
 */
class PipeHostTest {

    @Test
    fun `companion host needs both an association and API 31`() {
        assertEquals(Host.COMPANION, chooseHost(31, hasAssociation = true))
        assertEquals(Host.COMPANION, chooseHost(36, hasAssociation = true))
    }

    @Test
    fun `no association means the foreground service, however new the platform`() {
        assertEquals(Host.FOREGROUND, chooseHost(36, hasAssociation = false))
        assertEquals(Host.FOREGROUND, chooseHost(31, hasAssociation = false))
    }

    @Test
    fun `below API 31 an association is not enough`() {
        // CompanionDeviceService and startObservingDevicePresence both arrive in 31.
        // An association can exist on 26+, so this is a real combination, not a
        // hypothetical one.
        assertEquals(Host.FOREGROUND, chooseHost(30, hasAssociation = true))
        assertEquals(Host.FOREGROUND, chooseHost(24, hasAssociation = true))
    }

    @Test
    fun `the minimum is Android 12`() {
        assertEquals(31, MIN_COMPANION_SDK)
        assertEquals(Host.FOREGROUND, chooseHost(MIN_COMPANION_SDK - 1, hasAssociation = true))
        assertEquals(Host.COMPANION, chooseHost(MIN_COMPANION_SDK, hasAssociation = true))
    }
}
