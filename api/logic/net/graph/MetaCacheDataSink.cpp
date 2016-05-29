#include "MetaCacheDataSink.h"
#include <QFile>
#include <QFileInfo>
#include "Env.h"
#include "FileSystem.h"

JobStatus MetaCacheDataSink::init(QNetworkRequest& request)
{
	auto path = m_entry->getFullPath();
	if (!m_entry->isStale())
	{
		return Job_Finished;
	}
	// create a new save file
	m_output_file.reset(new QSaveFile(path));

	// if there already is a file and md5 checking is in effect and it can be opened
	if (!FS::ensureFilePathExists(path))
	{
		qCritical() << "Could not create folder for " + path;
		return Job_Failed;
	}
	if (!m_output_file->open(QIODevice::WriteOnly))
	{
		qCritical() << "Could not open " + path + " for writing";
		return Job_Failed;
	}
	// check file consistency first.
	QFile current(path);
	if(current.exists() && current.size() != 0)
	{
		if (m_entry->getRemoteChangedTimestamp().size())
		{
			request.setRawHeader(QString("If-Modified-Since").toLatin1(), m_entry->getRemoteChangedTimestamp().toLatin1());
		}
		if (m_entry->getETag().size())
		{
			request.setRawHeader(QString("If-None-Match").toLatin1(), m_entry->getETag().toLatin1());
		}
	}
	return Job_InProgress;
}

JobStatus MetaCacheDataSink::finalize(QNetworkReply& reply)
{
	auto path = m_entry->getFullPath();
	// if we wrote any data to the save file, we try to commit the data to the real file.
	if (wroteAnyData)
	{
		// nothing went wrong...
		if (m_output_file->commit())
		{
			m_entry->setMD5Sum(m_md5Node->hash().toHex().constData());
		}
		else
		{
			qCritical() << "Failed to commit changes to " << path;
			m_output_file->cancelWriting();
			return Job_Failed;
		}
	}

	// then get rid of the save file
	m_output_file.reset();

	QFileInfo output_file_info(path);

	m_entry->setETag(reply.rawHeader("ETag").constData());
	if (reply.hasRawHeader("Last-Modified"))
	{
		m_entry->setRemoteChangedTimestamp(reply.rawHeader("Last-Modified").constData());
	}
	m_entry->setLocalChangedTimestamp(output_file_info.lastModified().toUTC().toMSecsSinceEpoch());
	m_entry->setStale(false);
	ENV.metacache()->updateEntry(m_entry);
	return Job_Finished;
}

JobStatus MetaCacheDataSink::abort()
{
	m_output_file->cancelWriting();
	return Job_Failed;
}

JobStatus MetaCacheDataSink::write(QByteArray& data)
{
	if (m_output_file->write(data) != data.size())
	{
		qCritical() << "Failed writing into " + m_entry->getFullPath();
		m_output_file->cancelWriting();
		m_output_file.reset();
		wroteAnyData = false;
		return Job_Failed;
	}
	wroteAnyData = true;
	return Job_InProgress;
}
