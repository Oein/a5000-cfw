import * as BackgroundTask from 'expo-background-task';
import * as TaskManager from 'expo-task-manager';

import { createCameraClient } from '@/lib/camera';
import { loadHost } from '@/lib/host';

import { BACKGROUND_BUDGET, syncOnce } from './syncEngine';

/**
 * The OS's periodic background task (iOS BGTaskScheduler / Android WorkManager): best effort,
 * no sooner than every 15 minutes and often much later. It catches up photos when geotagging
 * (the reliable wake-up source) is off. Imported by the root layout for defineTask.
 */

export const SYNC_TASK = 'a5k-photo-sync';

TaskManager.defineTask(SYNC_TASK, async () => {
  try {
    const client = createCameraClient(await loadHost());
    await syncOnce(client, BACKGROUND_BUDGET);
    return BackgroundTask.BackgroundTaskResult.Success;
  } catch {
    return BackgroundTask.BackgroundTaskResult.Failed;
  }
});

export async function setBackgroundSync(enabled: boolean) {
  const registered = await TaskManager.isTaskRegisteredAsync(SYNC_TASK).catch(() => false);
  if (enabled && !registered) {
    await BackgroundTask.registerTaskAsync(SYNC_TASK, { minimumInterval: 15 });
  } else if (!enabled && registered) {
    await BackgroundTask.unregisterTaskAsync(SYNC_TASK);
  }
}
