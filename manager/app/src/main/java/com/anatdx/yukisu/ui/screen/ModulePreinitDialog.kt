package com.anatdx.yukisu.ui.screen

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.anatdx.yukisu.R
import com.anatdx.yukisu.ui.component.YukiAlertDialog
import com.anatdx.yukisu.ui.util.getRootShell
import com.anatdx.yukisu.ui.util.ksudCmd
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject

private data class PreinitStatus(
    val supported: Boolean,
    val bootCount: Long,
    val bootloop: Boolean,
    val modules: List<Pair<String, String>>,
)

private suspend fun readPreinitStatus(): PreinitStatus = withContext(Dispatchers.IO) {
    val result = getRootShell().newJob().add(ksudCmd("module preinit status --json")).exec()
    check(result.isSuccess)
    val json = JSONObject(result.out.joinToString("\n"))
    val modules = json.getJSONArray("modules")
    PreinitStatus(
        json.getBoolean("supported"),
        json.getLong("boot_count"),
        json.getBoolean("bootloop"),
        List(modules.length()) { index ->
            val entry = modules.getJSONObject(index)
            entry.getString("id") to entry.getString("state")
        },
    )
}

@Composable
internal fun ModulePreinitDialog(onDismiss: () -> Unit, onReset: () -> Unit) {
    val scope = rememberCoroutineScope()
    var status by remember { mutableStateOf<PreinitStatus?>(null) }
    var busy by remember { mutableStateOf(true) }
    var failed by remember { mutableStateOf(false) }
    var resetDone by remember { mutableStateOf(false) }
    var confirmReset by remember { mutableStateOf(false) }
    suspend fun refresh() {
        busy = true
        failed = false
        try {
            status = readPreinitStatus()
        } catch (error: Exception) {
            if (error is CancellationException) throw error
            status = null
            failed = true
        } finally {
            busy = false
        }
    }
    LaunchedEffect(Unit) { refresh() }
    YukiAlertDialog(
        onDismissRequest = { if (!busy) onDismiss() },
        title = { Text(stringResource(R.string.module_preinit_title)) },
        text = {
            Column(
                Modifier.verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                if (busy) LinearProgressIndicator(Modifier.fillMaxWidth())
                if (failed) Text(stringResource(R.string.operation_failed))
                if (resetDone) Text(stringResource(R.string.module_preinit_reset_done))
                status?.let { current ->
                    Text(stringResource(
                        when {
                            !current.supported -> R.string.module_preinit_unsupported
                            current.bootloop -> R.string.module_preinit_bootloop
                            else -> R.string.module_preinit_ready
                        },
                    ))
                    Text(stringResource(R.string.module_preinit_boot_count, current.bootCount.coerceAtMost(3)))
                    if (current.modules.isEmpty()) Text(stringResource(R.string.module_preinit_empty))
                    current.modules.forEach { (id, state) ->
                        Column {
                            Text(id, style = MaterialTheme.typography.titleSmall)
                            Text(stringResource(when (state) {
                                "ok" -> R.string.module_preinit_staged
                                "bootloop" -> R.string.module_preinit_bootloop
                                "no_space" -> R.string.module_preinit_no_space
                                "unsupported" -> R.string.module_preinit_unsupported
                                else -> R.string.module_preinit_error
                            }), style = MaterialTheme.typography.bodySmall)
                        }
                    }
                    Text(stringResource(R.string.module_preinit_manage_hint), style = MaterialTheme.typography.bodySmall)
                    OutlinedButton(
                        onClick = { confirmReset = true },
                        enabled = !busy && current.bootCount > 0,
                    ) { Text(stringResource(R.string.module_preinit_reset)) }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss, enabled = !busy) { Text(stringResource(android.R.string.ok)) }
        },
        dismissButton = {
            TextButton(onClick = { scope.launch { refresh() } }, enabled = !busy) {
                Text(stringResource(R.string.refresh))
            }
        },
    )
    if (confirmReset) {
        YukiAlertDialog(
            onDismissRequest = { confirmReset = false },
            title = { Text(stringResource(R.string.module_preinit_reset)) },
            text = { Text(stringResource(R.string.module_preinit_reset_confirm)) },
            confirmButton = {
                TextButton(onClick = {
                    confirmReset = false
                    busy = true
                    failed = false
                    resetDone = false
                    scope.launch {
                        try {
                            val success = withContext(Dispatchers.IO) {
                                getRootShell().newJob().add(ksudCmd("module preinit reset")).exec().isSuccess
                            }
                            if (success) {
                                resetDone = true
                                onReset()
                                refresh()
                            } else {
                                failed = true
                            }
                        } catch (error: Exception) {
                            if (error is CancellationException) throw error
                            failed = true
                        } finally {
                            busy = false
                        }
                    }
                }) { Text(stringResource(android.R.string.ok)) }
            },
            dismissButton = {
                TextButton(onClick = { confirmReset = false }) { Text(stringResource(android.R.string.cancel)) }
            },
        )
    }
}
