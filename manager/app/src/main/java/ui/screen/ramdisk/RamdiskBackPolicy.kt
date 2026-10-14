package ui.screen.ramdisk

/** A single decision shared by toolbar, local back and host leave protection. */
internal enum class RamdiskBackAction {
    Blocked, ConfirmText, CloseText, CloseElf, ClearSelection, StopSearch,
    ParentDirectory, FragmentSelector, ConfirmArchive, PrimeExit, Exit,
}

internal data class RamdiskBackContext(
    val busy: Boolean = false,
    val textOpen: Boolean = false,
    val textDirty: Boolean = false,
    val elfOpen: Boolean = false,
    val selecting: Boolean = false,
    val searching: Boolean = false,
    val hasParent: Boolean = false,
    val multipleFragments: Boolean = false,
    val archiveChanged: Boolean = false,
    val exitPrimed: Boolean = false,
) {
    fun action(leavingScreen: Boolean = false): RamdiskBackAction = when {
        busy -> RamdiskBackAction.Blocked
        textOpen && textDirty -> RamdiskBackAction.ConfirmText
        leavingScreen && archiveChanged -> RamdiskBackAction.ConfirmArchive
        leavingScreen -> RamdiskBackAction.Exit
        textOpen -> RamdiskBackAction.CloseText
        elfOpen -> RamdiskBackAction.CloseElf
        selecting -> RamdiskBackAction.ClearSelection
        searching -> RamdiskBackAction.StopSearch
        hasParent -> RamdiskBackAction.ParentDirectory
        multipleFragments -> RamdiskBackAction.FragmentSelector
        archiveChanged -> RamdiskBackAction.ConfirmArchive
        exitPrimed -> RamdiskBackAction.Exit
        else -> RamdiskBackAction.PrimeExit
    }
}
