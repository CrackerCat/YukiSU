package com.anatdx.yukisu.ui.component

import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import com.anatdx.yukisu.ui.theme.CardStyleProvider.getTopAppBarColors
import com.anatdx.yukisu.ui.theme.LocalCompactWindow
import com.anatdx.yukisu.ui.theme.isExpressiveUi

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun rememberYukiTopAppBarScrollBehavior(
    state: TopAppBarState = rememberTopAppBarState(),
): TopAppBarScrollBehavior =
    if (isExpressiveUi && !LocalCompactWindow.current) {
        TopAppBarDefaults.exitUntilCollapsedScrollBehavior(state)
    } else {
        TopAppBarDefaults.pinnedScrollBehavior(state)
    }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun YukiLargeTopAppBar(
    title: @Composable () -> Unit,
    navigationIcon: @Composable () -> Unit = {},
    actions: @Composable RowScope.() -> Unit = {},
    colors: TopAppBarColors = getTopAppBarColors(),
    windowInsets: WindowInsets = TopAppBarDefaults.windowInsets,
    scrollBehavior: TopAppBarScrollBehavior? = null,
) {
    if (LocalCompactWindow.current) {
        TopAppBar(
            title = title,
            navigationIcon = navigationIcon,
            actions = actions,
            colors = colors,
            windowInsets = windowInsets,
        )
    } else {
        LargeFlexibleTopAppBar(
            title = title,
            navigationIcon = navigationIcon,
            actions = actions,
            colors = colors,
            windowInsets = windowInsets,
            scrollBehavior = scrollBehavior,
        )
    }
}
