/*
   Copyright 2016 Skytechnology sp. z o.o.

   This file is part of LizardFS.

   LizardFS is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, version 3.

   LizardFS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with LizardFS. If not, see <http://www.gnu.org/licenses/>.
*/

#include "common/platform.h"

#include "master/task_manager.h"

#include "common/loop_watchdog.h"
#include "protocol/MFSCommunication.h"

void TaskManager::Job::finalizeTask(TaskIterator itask, int status) {
	if (status || (itask->isFinished() && tasks_.size() <= 1)) {
		if (finish_callback_) {
			finish_callback_(status);
		}
		tasks_.clear_and_dispose([](Task *ptr) { delete ptr; });
	} else if (itask->isFinished()) {
		tasks_.erase_and_dispose(itask, [](Task *ptr) { delete ptr; });
	}
}

void TaskManager::Job::processTask(uint32_t ts) {
	if (!tasks_.empty()) {
		auto i_front = tasks_.begin();
		int status = i_front->execute(ts, tasks_);
		finalizeTask(i_front, status);
	}
}

int TaskManager::submitTask(uint32_t ts, int initial_batch_size, Task *task,
			    const std::function<void(int)> &callback) {
	Job new_job;

	int done = 0;
	int status = LIZARDFS_STATUS_OK;

	new_job.setFinishCallback([&done, &status](int code) {
		status = code;
		done = 1;
	});

	new_job.addTask(task);
	for (int i = 0; i < initial_batch_size; i++) {
		new_job.processTask(ts);
		if (new_job.isFinished()) {
			break;
		}
	}

	if (done) {
		assert(new_job.isFinished());
		return status;
	}

	new_job.setFinishCallback(callback);
	job_list_.push_back(std::move(new_job));

	return LIZARDFS_ERROR_WAITING;
}

void TaskManager::processJobs(uint32_t ts, int number_of_tasks) {
	SignalLoopWatchdog watchdog;
	JobIterator it = job_list_.begin();
	watchdog.start();
	for (int i = 0; i < number_of_tasks; ++i) {
		if (it == job_list_.end() || watchdog.expired()) {
			break;
		}
		if (it->isFinished()) {
			it = job_list_.erase(it);
		} else {
			it->processTask(ts);
			++it;
		}
		if (it == job_list_.end()) {
			it = job_list_.begin();
		}
	}
}
