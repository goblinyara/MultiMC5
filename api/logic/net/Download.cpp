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

#include "Download.h"

#include <QFileInfo>
#include <QDateTime>
#include <QDebug>
#include "Env.h"
#include <FileSystem.h>
#include "graph/ChecksumNode.h"
#include "graph/MetaCacheDataSink.h"

Download::Download(QUrl url, MetaEntryPtr entry)
	: NetAction()
{
	m_url = url;
	auto md5Node = new ChecksumNode(QCryptographicHash::Md5);
	auto cachedNode = new MetaCacheDataSink(entry, md5Node);
	md5Node->setNext(cachedNode);
	m_graph.reset(md5Node);
	m_target_path = entry->getFullPath();
	m_status = Job_NotStarted;
}

void Download::start()
{
	QNetworkRequest request(m_url);
	m_status = m_graph->init(request);
	switch(m_status)
	{
		case Job_Finished:
			emit succeeded(m_index_within_job);
			qDebug() << "Download cache hit " << m_url.toString();
			return;
		case Job_InProgress:
			qDebug() << "Downloading " << m_url.toString();
			break;
		case Job_NotStarted:
		case Job_Failed:
			emit failed(m_index_within_job);
			return;
	}

	request.setHeader(QNetworkRequest::UserAgentHeader, "MultiMC/5.0");

	auto worker = ENV.qnam();
	QNetworkReply *rep = worker->get(request);

	m_reply.reset(rep);
	connect(rep, SIGNAL(downloadProgress(qint64, qint64)), SLOT(downloadProgress(qint64, qint64)));
	connect(rep, SIGNAL(finished()), SLOT(downloadFinished()));
	connect(rep, SIGNAL(error(QNetworkReply::NetworkError)), SLOT(downloadError(QNetworkReply::NetworkError)));
	connect(rep, SIGNAL(readyRead()), SLOT(downloadReadyRead()));
}

void Download::downloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
	m_total_progress = bytesTotal;
	m_progress = bytesReceived;
	emit netActionProgress(m_index_within_job, bytesReceived, bytesTotal);
}

void Download::downloadError(QNetworkReply::NetworkError error)
{
	// error happened during download.
	qCritical() << "Failed " << m_url.toString() << " with reason " << error;
	m_status = Job_Failed;
}

bool Download::handleRedirect()
{
	QVariant redirect = m_reply->header(QNetworkRequest::LocationHeader);
	QString redirectURL;
	if(redirect.isValid())
	{
		redirectURL = redirect.toString();
	}
	// FIXME: This is a hack for https://bugreports.qt-project.org/browse/QTBUG-41061
	else if(m_reply->hasRawHeader("Location"))
	{
		auto data = m_reply->rawHeader("Location");
		if(data.size() > 2 && data[0] == '/' && data[1] == '/')
			redirectURL = m_reply->url().scheme() + ":" + data;
	}
	if (!redirectURL.isEmpty())
	{
		m_url = QUrl(redirect.toString());
		qDebug() << "Following redirect to " << m_url.toString();
		start();
		return true;
	}
	return false;
}


void Download::downloadFinished()
{
	// handle HTTP redirection first
	if(handleRedirect())
	{
		return;
	}

	// if the download failed before this point ...
	if (m_status == Job_Failed)
	{
		m_graph->abort();
		m_reply.reset();
		emit failed(m_index_within_job);
		return;
	}

	// otherwise, finalize the whole graph
	m_status = m_graph->finalize(*m_reply.get());
	if (m_status == Job_Failed)
	{
		m_graph->abort();
		m_reply.reset();
		emit failed(m_index_within_job);
		return;
	}
	m_reply.reset();
	emit succeeded(m_index_within_job);
}

void Download::downloadReadyRead()
{
	auto data = m_reply->readAll();
	m_status = m_graph->write(data);
	if(m_status == Job_Failed)
	{
		emit failed(m_index_within_job);
	}
}

