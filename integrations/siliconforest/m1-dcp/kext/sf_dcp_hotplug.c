// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Publish cached connector changes to Darwin without a userspace udev.
 * Called after Linux DCP's real hotplug notification, never a fake event.
 * A dedicated worker refreshes modes before publishing the generation;
 * sysctl readers never perform detection, mailbox requests or modesets.
 */
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_lock.h>
static DEFINE_MUTEX(sf_hotplug_lock);
static struct drm_device *sf_hotplug_card;
static struct workqueue_struct *sf_hotplug_queue;
static char sf_hotplug_text[1024];
static uint64_t sf_hotplug_sequence;
static void sf_hotplug_snapshot(struct work_struct *work)
{
    char text[sizeof(sf_hotplug_text)]={0};size_t used=0;
    struct drm_connector_list_iter iter;
    struct drm_connector *connector;
    struct drm_device *dev;
    mutex_lock(&sf_hotplug_lock);dev=sf_hotplug_card;mutex_unlock(&sf_hotplug_lock);
    if(!dev)return;
    // Refresh on our dedicated queue, outside the RTKit/HPD/system work
    // threads. A slow EDID/AUX probe cannot block sysctl reads of the last
    // published cache. DRM ioctls still obey DRM locks; their latency must
    // be measured on hardware. Same locking as dcp_external_relink().
    mutex_lock(&dev->mode_config.mutex);
    drm_connector_list_iter_begin(dev,&iter);
    drm_for_each_connector_iter(connector,&iter) {
        if(connector->funcs->fill_modes)
            connector->funcs->fill_modes(connector,dev->mode_config.max_width,dev->mode_config.max_height);
    }
    drm_connector_list_iter_end(&iter);
    mutex_unlock(&dev->mode_config.mutex);
    // Run off the RTKit/HPD caller's thread, outside its locks. Connector
    // mode/status caches use the DRM connection mutex; no forced detection.
    drm_modeset_lock_all(dev);
    drm_connector_list_iter_begin(dev,&iter);
    drm_for_each_connector_iter(connector,&iter) {
        struct drm_display_mode *mode,*preferred=NULL;
        list_for_each_entry(mode,&connector->modes,head) {
            if(!preferred)preferred=mode;
            if(mode->type&DRM_MODE_TYPE_PREFERRED){preferred=mode;break;}
        }
        const char *state=connector->status==connector_status_connected?"connected":
            connector->status==connector_status_disconnected?"disconnected":"unknown";
        used+=scnprintf(text+used,sizeof(text)-used,"%u %s %s",connector->base.id,connector->name,state);
        if(preferred)used+=scnprintf(text+used,sizeof(text)-used," %ux%u@%u%s",preferred->hdisplay,
            preferred->vdisplay,drm_mode_vrefresh(preferred),preferred->type&DRM_MODE_TYPE_PREFERRED?" preferred":"");
        used+=scnprintf(text+used,sizeof(text)-used,"\n");
    }
    drm_connector_list_iter_end(&iter);
    drm_modeset_unlock_all(dev);
    mutex_lock(&sf_hotplug_lock);
    memcpy(sf_hotplug_text,text,sizeof(text));
    // Snapshot and DRM caches are visible before the generation is published.
    if(sf_hotplug_sequence!=UINT64_MAX)++sf_hotplug_sequence;
    mutex_unlock(&sf_hotplug_lock);
}
static DECLARE_WORK(sf_hotplug_work,sf_hotplug_snapshot);
void sf_dcp_note_hotplug(struct drm_connector *connector)
{
    if(!connector || !connector->dev)return;
    mutex_lock(&sf_hotplug_lock);
    if(!sf_hotplug_card){sf_hotplug_card=connector->dev;drm_dev_get(sf_hotplug_card);}
    if(!sf_hotplug_queue)sf_hotplug_queue=alloc_ordered_workqueue("sfdcp_hpd",WQ_UNBOUND);
    struct workqueue_struct *queue=sf_hotplug_queue;
    bool same=sf_hotplug_card==connector->dev;
    mutex_unlock(&sf_hotplug_lock);
    // One aggregate apple DRM device is the supported T8103 topology. The
    // retained reference keeps queued snapshot work valid across cable loss.
    if(same && queue)queue_work(queue,&sf_hotplug_work);
    else if(!queue)pr_err("SFDcp: hotplug cache queue allocation failed; no generation published\n");
}
uint64_t sf_dcp_hotplug_seq(void)
{
    uint64_t value;mutex_lock(&sf_hotplug_lock);value=sf_hotplug_sequence;mutex_unlock(&sf_hotplug_lock);return value;
}
void sf_dcp_hotplug_connectors(char *dest,size_t bytes)
{
    if(!dest || !bytes)return;
    mutex_lock(&sf_hotplug_lock);strlcpy(dest,sf_hotplug_text,bytes);mutex_unlock(&sf_hotplug_lock);
}
