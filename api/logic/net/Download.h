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

#include "NetAction.h"
#include "HttpMetaCache.h"
#include "graph/DataNode.h"

#include "multimc_logic_export.h"

typedef std::shared_ptr<class Download> DownloadPtr;
class MULTIMC_LOGIC_EXPORT Download : public NetAction
{
	Q_OBJECT
public: /* con/des */
	explicit Download(QUrl url, MetaEntryPtr entry);
	virtual ~Download(){};
	static DownloadPtr make(QUrl url, MetaEntryPtr entry)
	{
		return std::make_shared<Download>(url, entry);
	}

public: /* methods */
	QString getTargetFilepath()
	{
		return m_target_path;
	}

private: /* methods */
	bool handleRedirect();

protected slots:
	virtual void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
	virtual void downloadError(QNetworkReply::NetworkError error);
	virtual void downloadFinished();
	virtual void downloadReadyRead();

public slots:
	virtual void start();

private: /* data */
	QString m_target_path;
	std::unique_ptr<class DataNode> m_graph;
};
