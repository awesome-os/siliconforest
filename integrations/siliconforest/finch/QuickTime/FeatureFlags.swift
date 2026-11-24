// SPDX-License-Identifier: MIT OR Apache-2.0
// Real Swift protocol metadata and requirement descriptors. No system feature
// services are available through this minimal provider.
public protocol FeatureFlagsKey {
  var domain: StaticString { get }
  var feature: StaticString { get }
}
public func isFeatureEnabled(_ flag: any FeatureFlagsKey) -> Bool {
  _ = flag.domain
  _ = flag.feature
  return false
}
