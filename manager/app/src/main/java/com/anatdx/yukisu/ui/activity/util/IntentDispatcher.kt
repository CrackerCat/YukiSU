package com.anatdx.yukisu.ui.activity.util

import android.content.Intent
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import com.anatdx.yukisu.ui.component.ZipFileDetector
import com.anatdx.yukisu.ui.component.ZipFileInfo
import kotlinx.coroutines.channels.Channel

internal class IntentQueue<T : Any> {
    private val ready = Channel<Unit>(Channel.CONFLATED)
    private val queued = ArrayDeque<T>()
    private var pending: T? = null

    fun add(value: T) {
        queued.addLast(value)
        ready.trySend(Unit).getOrThrow()
    }

    suspend fun next(): T {
        pending?.let { return it }
        // Keep payloads outside the channel so cancellation during receive cannot discard them.
        while (queued.isEmpty()) ready.receive()
        return queued.removeFirst().also { pending = it }
    }

    fun complete() {
        pending = null
    }

    fun close() = ready.close()
}

class IntentDispatcher : ViewModel() {
    internal val queue = IntentQueue<Intent>()
    var zipFiles by mutableStateOf<List<ZipFileInfo>>(emptyList())
    var showConfirmation by mutableStateOf(false)

    fun submit(intent: Intent) = queue.add(Intent(intent))

    override fun onCleared() {
        queue.close()
        ZipFileDetector.cleanupStagedFiles(zipFiles)
    }
}
