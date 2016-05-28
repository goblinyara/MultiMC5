/* Copyright 2013-2015 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once
#include <QtNetwork>
#include "NetAction.h"
#include "ByteArrayDownload.h"
#include "MD5EtagDownload.h"
#include "Download.h"
#include "HttpMetaCache.h"
#include "tasks/Task.h"
#include "QObjectPtr.h"

#include "multimc_logic_export.h"

class NetJob;
typedef shared_qobject_ptr<NetJob> NetJobPtr;

class MULTIMC_LOGIC_EXPORT NetJob : public Task
{
	Q_OBJECT
public:
	explicit NetJob(QString job_name) : Task(), m_job_name(job_name) {}
	virtual ~NetJob() {}
	bool addNetAction(NetActionPtr action)
	{
		action->m_index_within_job = downloads.size();
		downloads.append(action);
		part_info pi;
		{
			pi.current_progress = action->currentProgress();
			pi.total_progress = action->totalProgress();
			pi.failures = action->numberOfFailures();
		}
		parts_progress.append(pi);
		total_progress += pi.total_progress;
		// if this is already running, the action needs to be started right away!
		if (isRunning())
		{
			setProgress(current_progress, total_progress);
			connect(action.get(), SIGNAL(succeeded(int)), SLOT(partSucceeded(int)));
			connect(action.get(), SIGNAL(failed(int)), SLOT(partFailed(int)));
			connect(action.get(), SIGNAL(netActionProgress(int, qint64, qint64)),
					SLOT(partProgress(int, qint64, qint64)));
			action->start();
		}
		return true;
	}

	NetActionPtr operator[](int index)
	{
		return downloads[index];
	}
	const NetActionPtr at(const int index)
	{
		return downloads.at(index);
	}
	NetActionPtr first()
	{
		if (downloads.size())
			return downloads[0];
		return NetActionPtr();
	}
	int size() const
	{
		return downloads.size();
	}
	virtual bool isRunning() const
	{
		return m_running;
	}
	QStringList getFailedFiles();

private slots:
	void startMoreParts();

public slots:
	virtual void executeTask();
	// FIXME: implement
	virtual bool abort() {return false;};

private slots:
	void partProgress(int index, qint64 bytesReceived, qint64 bytesTotal);
	void partSucceeded(int index);
	void partFailed(int index);

private:
	struct part_info
	{
		qint64 current_progress = 0;
		qint64 total_progress = 1;
		int failures = 0;
		bool connected = false;
	};
	QString m_job_name;
	QList<NetActionPtr> downloads;
	QList<part_info> parts_progress;
	QQueue<int> m_todo;
	QSet<int> m_doing;
	QSet<int> m_done;
	QSet<int> m_failed;
	qint64 current_progress = 0;
	qint64 total_progress = 0;
	bool m_running = false;
};
