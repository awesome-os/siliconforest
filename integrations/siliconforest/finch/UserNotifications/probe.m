/* SPDX-License-Identifier: MIT */
#import <UserNotifications/UserNotifications.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
int main(void) {
    @autoreleasepool {
        UNMutableNotificationContent *content = [UNMutableNotificationContent new];
        content.title = @"Finch conformance";
        content.body = @"CLI-only local notification";
        UNTimeIntervalNotificationTrigger *trigger = [UNTimeIntervalNotificationTrigger triggerWithTimeInterval:0.05 repeats:NO];
        assert(trigger.timeInterval == 0.05 && !trigger.repeats && trigger.nextTriggerDate);
        UNNotificationRequest *request = [UNNotificationRequest requestWithIdentifier:@"org.siliconforest.conformance" content:content trigger:trigger];
        content.title = @"changed after copy";
        assert([request.content.title isEqualToString:@"Finch conformance"]);
        /* Volatile test configuration: no system permission/defaults file is changed. */
        [[NSUserDefaults standardUserDefaults] setVolatileDomain:@{@"FinchUserNotificationsAuthorization":@2} forName:NSArgumentDomain];
        UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
        __block BOOL okay = NO;
        [center addNotificationRequest:request withCompletionHandler:^(NSError *error) {
            okay = error == nil;
            dispatch_semaphore_signal(semaphore);
        }];
        assert(!dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 3*NSEC_PER_SEC)) && okay);
        usleep(250000);
        [center getDeliveredNotificationsWithCompletionHandler:^(NSArray<UNNotification *> *notifications) {
            okay = notifications.count == 1 && [notifications[0].request.identifier isEqualToString:request.identifier];
            dispatch_semaphore_signal(semaphore);
        }];
        assert(!dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 3*NSEC_PER_SEC)) && okay);
        [center removeAllDeliveredNotifications];
        [center getDeliveredNotificationsWithCompletionHandler:^(NSArray *notifications) {
            okay = !notifications.count;
            dispatch_semaphore_signal(semaphore);
        }];
        assert(!dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 3*NSEC_PER_SEC)) && okay);
        puts("UserNotifications: PASS (content copy, interval timer, delivered list, removal; no banners)");
    }
    return 0;
}
