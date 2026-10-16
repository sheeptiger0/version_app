package com.example.fatigueguard.proto

import java.nio.ByteBuffer
import java.nio.ByteOrder

// Wire protocol shared with the C++ server. See server/src/net/Protocol.h.
object FrameProtocol {
    const val MAGIC = 0xFACE
    const val HEADER_SIZE = 11
    const val MAX_PAYLOAD = 1200

    const val TYPE_VIDEO = 0x01
    const val TYPE_HELLO = 0x02
    const val TYPE_STATE = 0x03

    data class Header(
        val magic: Int,
        val type: Int,
        val frameId: Long,
        val fragIdx: Int,
        val fragCnt: Int,
    )

    fun packHeader(type: Int, frameId: Long, fragIdx: Int, fragCnt: Int): ByteArray {
        val b = ByteBuffer.allocate(HEADER_SIZE).order(ByteOrder.BIG_ENDIAN)
        b.putShort(MAGIC.toShort())
        b.put(type.toByte())
        b.putInt(frameId.toInt())
        b.putShort(fragIdx.toShort())
        b.putShort(fragCnt.toShort())
        return b.array()
    }

    fun unpackHeader(buf: ByteArray, len: Int): Header? {
        if (len < HEADER_SIZE) return null
        val b = ByteBuffer.wrap(buf, 0, HEADER_SIZE).order(ByteOrder.BIG_ENDIAN)
        val magic = b.short.toInt() and 0xFFFF
        if (magic != MAGIC) return null
        val type = b.get().toInt() and 0xFF
        val frameId = b.int.toLong() and 0xFFFFFFFFL
        val fragIdx = b.short.toInt() and 0xFFFF
        val fragCnt = b.short.toInt() and 0xFFFF
        return Header(magic, type, frameId, fragIdx, fragCnt)
    }

    // Split a JPEG payload into one datagram per fragment.
    fun fragment(payload: ByteArray, frameId: Long): List<ByteArray> {
        val fragCnt = (payload.size + MAX_PAYLOAD - 1) / MAX_PAYLOAD
        if (fragCnt == 0) return emptyList()
        val out = ArrayList<ByteArray>(fragCnt)
        var off = 0
        var idx = 0
        while (off < payload.size) {
            val n = minOf(MAX_PAYLOAD, payload.size - off)
            val header = packHeader(TYPE_VIDEO, frameId, idx, fragCnt)
            val dg = ByteArray(header.size + n)
            System.arraycopy(header, 0, dg, 0, header.size)
            System.arraycopy(payload, off, dg, header.size, n)
            out.add(dg)
            off += n
            idx++
        }
        return out
    }
}
