#pragma once
#include "DataNode.h"
#include "ChecksumNode.h"
#include "net/HttpMetaCache.h"
#include <QSaveFile>

class MetaCacheDataSink : public DataNode
{
public: /* con/des */
	MetaCacheDataSink(MetaEntryPtr entry, ChecksumNode * md5sum):m_entry(entry), m_md5Node(md5sum){};
	virtual ~MetaCacheDataSink() {};

public: /* methods */
	JobStatus init(QNetworkRequest & request) override;

	JobStatus write(QByteArray & data) override
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

	JobStatus abort() override;

	JobStatus finalize(QNetworkReply & reply) override;

private: /* data */
	MetaEntryPtr m_entry;
	ChecksumNode * m_md5Node;
	bool wroteAnyData = false;
	std::unique_ptr<QSaveFile> m_output_file;
};
