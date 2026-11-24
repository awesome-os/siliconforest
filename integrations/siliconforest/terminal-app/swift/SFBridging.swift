// SPDX-License-Identifier: Apache-2.0 WITH Swift-exception
// Foundation overlay: Dictionary <-> NSDictionary and Int, Bool <-> NSNumber, the bridging
// entry points Swift code built against Apple's SDK calls in Foundation (first user: Apple's
// Terminal.app). The shape is the Swift project's own overlay (stdlib/public/Darwin/Foundation,
// swift-5.4.3: NSDictionary.swift, NSNumber.swift); the bodies are written for this system's
// Foundation headers and convert element by element where the original used buffer tricks.
@_exported import Foundation

extension Dictionary : _ObjectiveCBridgeable {
  @_semantics("convertToObjectiveC")
  public func _bridgeToObjectiveC() -> NSDictionary {
    return unsafeBitCast(_bridgeToObjectiveCImpl(), to: NSDictionary.self)
  }

  private static func _sfConvert(_ d: NSDictionary, force: Bool) -> Dictionary? {
    if let native = [Key : Value]._bridgeFromObjectiveCAdoptingNativeStorageOf(d as AnyObject) {
      return native
    }
    let keys: [Any] = d.allKeys() ?? []
    var out = Dictionary(minimumCapacity: keys.count)
    for anyKey in keys {
      guard let anyValue = d.object(forKey: anyKey) else { continue }
      let keyObject = anyKey as AnyObject
      let valueObject = anyValue as AnyObject
      if force {
        out[Swift._forceBridgeFromObjectiveC(keyObject, Key.self)] = Swift._forceBridgeFromObjectiveC(valueObject, Value.self)
      } else {
        guard let k = Swift._conditionallyBridgeFromObjectiveC(keyObject, Key.self),
              let v = Swift._conditionallyBridgeFromObjectiveC(valueObject, Value.self) else { return nil }
        out[k] = v
      }
    }
    return out
  }

  public static func _forceBridgeFromObjectiveC(_ d: NSDictionary, result: inout Dictionary?) {
    result = _sfConvert(d, force: true)
  }

  public static func _conditionallyBridgeFromObjectiveC(_ x: NSDictionary, result: inout Dictionary?) -> Bool {
    result = _sfConvert(x, force: false)
    return result != nil
  }

  @_effects(readonly)
  public static func _unconditionallyBridgeFromObjectiveC(_ d: NSDictionary?) -> Dictionary {
    guard let d = d else { return Dictionary() }
    return _sfConvert(d, force: true)!
  }
}

extension Int : _ObjectiveCBridgeable {
  @_semantics("convertToObjectiveC")
  public func _bridgeToObjectiveC() -> NSNumber {
    return NSNumber(integer: self)
  }
  public static func _forceBridgeFromObjectiveC(_ x: NSNumber, result: inout Int?) {
    result = x.integerValue
  }
  public static func _conditionallyBridgeFromObjectiveC(_ x: NSNumber, result: inout Int?) -> Bool {
    result = x.integerValue
    return true
  }
  @_effects(readonly)
  public static func _unconditionallyBridgeFromObjectiveC(_ source: NSNumber?) -> Int {
    return source?.integerValue ?? 0
  }
}

extension Bool : _ObjectiveCBridgeable {
  @_semantics("convertToObjectiveC")
  public func _bridgeToObjectiveC() -> NSNumber {
    return NSNumber(bool: self)
  }
  public static func _forceBridgeFromObjectiveC(_ x: NSNumber, result: inout Bool?) {
    result = x.boolValue
  }
  public static func _conditionallyBridgeFromObjectiveC(_ x: NSNumber, result: inout Bool?) -> Bool {
    result = x.boolValue
    return true
  }
  @_effects(readonly)
  public static func _unconditionallyBridgeFromObjectiveC(_ source: NSNumber?) -> Bool {
    return source?.boolValue ?? false
  }
}
