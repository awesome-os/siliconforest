/* SPDX-License-Identifier: MIT */
/* Fresh local SQLite store only; suitable for same-binary native comparison. */
#import <CoreData/CoreData.h>
#import <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <dlfcn.h>
#include <unistd.h>
static void crashed(int sig) {
    void *frames[32];
    int (*bt)(void **,int)=dlsym(RTLD_DEFAULT,"backtrace");
    void (*symbols)(void *const *,int,int)=dlsym(RTLD_DEFAULT,"backtrace_symbols_fd");
    if (bt && symbols) symbols(frames,bt(frames,32),2);
    _exit(128+sig);
}
static void phase(const char *name) { fprintf(stderr,"phase %s\n",name); }
static void require(BOOL ok,const char *why) { if (!ok) { fprintf(stderr,"FAIL: %s\n",why);exit(1); } }
static NSPersistentStoreCoordinator *coordinator(NSManagedObjectModel *model,NSURL *url,NSPersistentStore **store) {
    NSError *error=nil;
    NSPersistentStoreCoordinator *c=[[NSPersistentStoreCoordinator alloc] initWithManagedObjectModel:model];
    *store=[c addPersistentStoreWithType:NSSQLiteStoreType configuration:nil URL:url options:nil error:&error];
    if (!*store) fprintf(stderr,"store error: %s\n",error.description.UTF8String);
    require(*store!=nil,"SQLite store opens");return c;
}
static NSManagedObjectContext *context(NSPersistentStoreCoordinator *coordinator) {
    NSManagedObjectContext *c=[[NSManagedObjectContext alloc] initWithConcurrencyType:NSPrivateQueueConcurrencyType];
    c.persistentStoreCoordinator=coordinator;return c;
}
int main(int argc,char **argv) {
    signal(SIGSEGV,crashed);
    require(argc==2,"usage: coredata-sqlite-probe NEW_STORE_PATH");
    @autoreleasepool {
        NSString *path=[NSString stringWithUTF8String:argv[1]];
        if (![path hasPrefix:@"/"]) path=[[[NSFileManager defaultManager] currentDirectoryPath] stringByAppendingPathComponent:path];
        require(![[NSFileManager defaultManager] fileExistsAtPath:path],"store path must be new");
        NSURL *url=[NSURL fileURLWithPath:path];
        phase("model");
        NSAttributeDescription *name=[[NSAttributeDescription alloc] init];name.name=@"name";name.attributeType=NSStringAttributeType;name.optional=NO;
        NSAttributeDescription *count=[[NSAttributeDescription alloc] init];count.name=@"count";count.attributeType=NSInteger64AttributeType;count.optional=NO;count.defaultValue=@0;
        NSEntityDescription *entity=[[NSEntityDescription alloc] init];entity.name=@"Item";entity.managedObjectClassName=@"NSManagedObject";entity.properties=@[name,count];
        NSManagedObjectModel *model=[[NSManagedObjectModel alloc] init];model.entities=@[entity];
        phase("first store");
        NSPersistentStore *store=nil;NSPersistentStoreCoordinator *coord=coordinator(model,url,&store);NSManagedObjectContext *c=context(coord);
        phase("first block");
        [c performBlockAndWait:^{
            phase("insert");
            NSArray *names=@[@"alpha",@"beta",@"gamma"];
            for (NSUInteger i=0;i<3;i++) {
                NSManagedObject *o=[NSEntityDescription insertNewObjectForEntityForName:@"Item" inManagedObjectContext:c];
                [o setValue:names[i] forKey:@"name"];[o setValue:@((i+1)*2) forKey:@"count"];
            }
            phase("insert save");
            NSError *error=nil;require([c save:&error],"insert save");[c reset];
        }];
        require([coord removePersistentStore:store error:NULL],"first store closes");
        phase("reopen");
        NSPersistentStore *reopened=nil;NSPersistentStoreCoordinator *next=coordinator(model,url,&reopened);NSManagedObjectContext *read=context(next);
        phase("read block");
        [read performBlockAndWait:^{
            NSError *error=nil;NSFetchRequest *all=[NSFetchRequest fetchRequestWithEntityName:@"Item"];
            all.sortDescriptors=@[[NSSortDescriptor sortDescriptorWithKey:@"name" ascending:YES]];
            NSArray *rows=[read executeFetchRequest:all error:&error];require(rows.count==3,"three persisted rows");
            NSFetchRequest *filtered=[NSFetchRequest fetchRequestWithEntityName:@"Item"];
            filtered.predicate=[NSPredicate predicateWithFormat:@"count >= %d",4];filtered.sortDescriptors=all.sortDescriptors;
            NSArray *selected=[read executeFetchRequest:filtered error:&error];require(selected.count==2,"predicate fetch");
            NSManagedObject *beta=selected[0],*gamma=selected[1];require([[beta valueForKey:@"name"] isEqual:@"beta"],"sorted beta");
            [beta setValue:@9 forKey:@"count"];[read deleteObject:gamma];require([read save:&error],"update delete save");[read reset];
            rows=[read executeFetchRequest:all error:&error];require(rows.count==2,"two final rows");
            require([[[rows[1] valueForKey:@"name"] description] isEqual:@"beta"] && [[rows[1] valueForKey:@"count"] longLongValue]==9,"persisted update");
            printf("SQLite insert=3 reopen=3 predicate=2 update=9 delete=1 final=2\n");[read reset];
        }];
        require([next removePersistentStore:reopened error:NULL],"second store closes");
        phase("SQLite inspection");
        sqlite3 *db=NULL;require(sqlite3_open_v2(path.fileSystemRepresentation,&db,SQLITE_OPEN_READONLY,NULL)==SQLITE_OK,"SQLite read-only inspection");
        sqlite3_stmt *statement=NULL;
        require(sqlite3_prepare_v2(db,"SELECT count(*),sum(ZCOUNT) FROM ZITEM",-1,&statement,NULL)==SQLITE_OK,"ZITEM table");
        require(sqlite3_step(statement)==SQLITE_ROW && sqlite3_column_int(statement,0)==2 && sqlite3_column_int(statement,1)==11,"ZITEM stored values");sqlite3_finalize(statement);
        require(sqlite3_prepare_v2(db,"SELECT count(*) FROM Z_METADATA",-1,&statement,NULL)==SQLITE_OK,"Z_METADATA table");
        require(sqlite3_step(statement)==SQLITE_ROW && sqlite3_column_int(statement,0)>0,"metadata persisted");sqlite3_finalize(statement);require(sqlite3_close(db)==SQLITE_OK,"SQLite inspection closes");
        printf("PASS: ZITEM/Z_METADATA persisted; fresh local store, no service\n");
    }
    return 0;
}
