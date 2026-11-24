// SPDX-License-Identifier: MIT
import FeatureFlags
struct OwnFlag: FeatureFlagsKey {
  var domain: StaticString { "org.siliconforest.own-probe" }
  var feature: StaticString { "unavailable" }
}
@inline(never) func lengths(_ flag: any FeatureFlagsKey) -> (Int,Int) {
  (flag.domain.utf8CodeUnitCount,flag.feature.utf8CodeUnitCount)
}
let flag: any FeatureFlagsKey = OwnFlag()
let counts=lengths(flag)
precondition(counts.0==27 && counts.1==11 && !isFeatureEnabled(flag))
print("PRIVATE_QT_SWIFT protocol=real witness=real enabled=false")
