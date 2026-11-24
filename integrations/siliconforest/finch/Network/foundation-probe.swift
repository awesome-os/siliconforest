// SPDX-License-Identifier: MIT
// Type-check only: value APIs required by Finch Network, no execution or I/O.
import Foundation
let bytes: [UInt8] = [1, 2, 3]
let data = Data(bytes)
let url = URL(string: "https://example.invalid/")
let uuid = UUID()
let lock = NSLock()
let count = lock.withLock { data.count }
