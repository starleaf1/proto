package link.dendritik.proto.pipe.nuron

import android.net.Uri

/**
 * The Nuron side of the provider contract, copied rather than shared.
 *
 * There is no library between these two apps and there should not be one for
 * four constants — but that means this file and
 * `com.amborjo.questjournal.provider.QuestFactsContract` are two halves of one
 * contract and must move together. A mismatched column name is a
 * `CursorIndexOutOfBoundsException` at read time, which is at least loud; a
 * mismatched *authority* is silently zero rows, which is not.
 */
object NuronContract {

    const val PACKAGE = "com.amborjo.questjournal"
    const val AUTHORITY = "com.amborjo.questjournal.questfacts"
    const val PERMISSION_READ = "com.amborjo.questjournal.permission.READ_QUEST_FACTS"

    val CONTENT_URI: Uri = Uri.parse("content://$AUTHORITY/facts")

    const val COL_ID = "_id"
    const val COL_START_EPOCH_S = "start_epoch_s"
    const val COL_DUR_MIN = "dur_min"
    const val COL_KIND = "kind"

    /** Wall-clock ms of Nuron's last successful reconciliation. 0 means never. */
    const val EXTRA_RECONCILED_AT_MS = "reconciled_at_ms"
    const val EXTRA_GENERATION = "generation"
}
