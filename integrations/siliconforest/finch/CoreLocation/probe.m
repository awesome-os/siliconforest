/* SPDX-License-Identifier: MIT */
#import <CoreLocation/CoreLocation.h>
#include <assert.h>
#include <stdio.h>
int main(void) {
    @autoreleasepool {
        CLLocationCoordinate2D p = CLLocationCoordinate2DMake(0, 0);
        assert(CLLocationCoordinate2DIsValid(p));
        assert(!CLLocationCoordinate2DIsValid(kCLLocationCoordinate2DInvalid));
        CLLocation *a = [[CLLocation alloc] initWithLatitude:0 longitude:0];
        CLLocation *b = [[CLLocation alloc] initWithLatitude:0 longitude:1];
        assert(a.coordinate.latitude == 0 && b.coordinate.longitude == 1);
        /* At the equator the ellipsoid arc is a * pi / 180 per degree. */
        double expected = 6378137.0 * M_PI / 180;
        assert(fabs([a distanceFromLocation:b]-expected) < 0.001);
        assert([a distanceFromLocation:a] == 0);
        assert(![CLLocationManager locationServicesEnabled]);
        CLLocationSourceInformation *info = [[CLLocationSourceInformation alloc]
            initWithSoftwareSimulationState:YES andExternalAccessoryState:NO];
        assert(info.isSimulatedBySoftware && !info.isProducedByAccessory);
        puts("CoreLocation: PASS (coordinate ABI, WGS84 equatorial distance, values, disabled service)");
    }
    return 0;
}
