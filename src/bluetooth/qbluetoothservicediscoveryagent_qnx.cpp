/***************************************************************************
**
** Copyright (C) 2012 Research In Motion
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtBluetooth module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qbluetoothservicediscoveryagent.h"
#include "qbluetoothservicediscoveryagent_p.h"

#include "qbluetoothdeviceinfo.h"
#include "qbluetoothdevicediscoveryagent.h"

#include <QStringList>
#include "qbluetoothuuid.h"

#include <sys/pps.h>
#ifdef QT_QNX_BT_BLUETOOTH
#include <errno.h>
#include <QPointer>
#endif

#include <QFile>

#include <QtCore/private/qcore_unix_p.h>

QT_BEGIN_NAMESPACE

#ifdef QT_QNX_BT_BLUETOOTH
void QBluetoothServiceDiscoveryAgentPrivate::deviceServicesDiscoveryCallback(bt_sdp_list_t *result, void *user_data, uint8_t error)
{
    if (error != 0)
        qWarning() << "Error received in callback: " << errno << strerror(errno);
    QPointer<QBluetoothServiceDiscoveryAgentPrivate> *classPointer = static_cast<QPointer<QBluetoothServiceDiscoveryAgentPrivate> *>(user_data);
    if (classPointer->isNull()) {
        qBBBluetoothDebug() << "Pointer received in callback is null";
        return;
    }
    QBluetoothServiceDiscoveryAgentPrivate *p = classPointer->data();
    if ( result == 0) {
        qBBBluetoothDebug() << "Result received in callback is null.";
        p->errorString = QBluetoothServiceDiscoveryAgent::tr("Result received in callback is null.");
        p->error = QBluetoothServiceDiscoveryAgent::InputOutputError;
        p->q_ptr->error(p->error);
        p->_q_serviceDiscoveryFinished();
        return;
    }

    for (int i = 0; i < result->num_records; i++) {
        bt_sdp_record_t rec = result->record[i];
        QBluetoothServiceInfo serviceInfo;
        serviceInfo.setDevice(p->discoveredDevices.at(0));
        serviceInfo.setServiceName(rec.name);
        serviceInfo.setServiceDescription(rec.description);
        //serviceInfo.setServiceAvailability(rec.availability);
        serviceInfo.setServiceProvider(QString(rec.provider));
        QBluetoothServiceInfo::Sequence  protocolDescriptorList;
        for ( int j = 0; j < rec.num_protocol; j++) {
            bt_sdp_prot_t protoc = rec.protocol[j];
            QString protocolUuid(protoc.uuid);
            protocolUuid = QStringLiteral("0x") + protocolUuid;
            QBluetoothUuid pUuid(protocolUuid.toUShort(0,0));
            protocolDescriptorList << QVariant::fromValue(pUuid);
            for ( int k = 0; k < 2; k++)
                protocolDescriptorList << QVariant::fromValue(QString::fromLatin1(protoc.parm[k]));
        }
        serviceInfo.setAttribute(QBluetoothServiceInfo::ProtocolDescriptorList, protocolDescriptorList);
        qBBBluetoothDebug() << "Service name " << rec.name << " Description: " << rec.description << "uuid " << rec.serviceId << "provider: " << rec.provider;
        qBBBluetoothDebug() << "num protocol " << rec.num_protocol << "record handle " << rec.record_handle << "class id" << rec.num_classId << "availability " << rec.availability << rec.num_language;

        QList<QBluetoothUuid> serviceClassId;

        for (int j = 0; j < rec.num_classId; j++) {
            bt_sdp_class_t uuid = rec.classId[j];
            qBBBluetoothDebug() << "uuid: " << uuid.uuid;
            QString protocolUuid(uuid.uuid);
            protocolUuid = QStringLiteral("0x") + protocolUuid;
            QBluetoothUuid Uuid(protocolUuid.toUShort(0,0));
            if (j == 0) {
                serviceInfo.setServiceUuid(Uuid);
                //Check if the UUID is in the uuidFilter
                if (!p->uuidFilter.isEmpty() && !p->uuidFilter.contains(Uuid))
                    continue;
            }
            serviceClassId << Uuid;
        }
        serviceInfo.setAttribute(QBluetoothServiceInfo::ServiceClassIds, QVariant::fromValue(serviceClassId));
        serviceInfo.setAttribute(QBluetoothServiceInfo::BrowseGroupList,
                                     QBluetoothUuid(QBluetoothUuid::PublicBrowseGroup));
        p->q_ptr->serviceDiscovered(serviceInfo);
    }
    p->_q_serviceDiscoveryFinished();
    //delete p;
    //delete classPointer;
}
#endif

QBluetoothServiceDiscoveryAgentPrivate::QBluetoothServiceDiscoveryAgentPrivate(const QBluetoothAddress &deviceAdapter)
    : m_rdfd(-1), rdNotifier(0), m_btInitialized(false), error(QBluetoothServiceDiscoveryAgent::NoError), deviceAddress(deviceAdapter), state(Inactive),
      deviceDiscoveryAgent(0), mode(QBluetoothServiceDiscoveryAgent::MinimalDiscovery)
{
    ppsRegisterControl();
    connect(&m_queryTimer, SIGNAL(timeout()), this, SLOT(queryTimeout()));
    ppsRegisterForEvent(QStringLiteral("service_updated"), this);
    //Needed for connecting signals and slots from static function
    qRegisterMetaType<QBluetoothServiceInfo>("QBluetoothServiceInfo");
    qRegisterMetaType<QBluetoothServiceDiscoveryAgent::Error>("QBluetoothServiceDiscoveryAgent::Error");
}

QBluetoothServiceDiscoveryAgentPrivate::~QBluetoothServiceDiscoveryAgentPrivate()
{
    ppsUnregisterForEvent(QStringLiteral("service_updated"), this);
    ppsUnregisterControl(this);
}

void QBluetoothServiceDiscoveryAgentPrivate::start(const QBluetoothAddress &address)
{
    Q_Q(QBluetoothServiceDiscoveryAgent);
#ifdef QT_QNX_BT_BLUETOOTH
    errno = 0;
    if (!m_btInitialized) {
        if (bt_device_init( 0 ) < 0) {
            qWarning() << "Failed to initialize bluetooth stack.";
            error = QBluetoothServiceDiscoveryAgent::InputOutputError;
            errorString = QBluetoothServiceDiscoveryAgent::tr("Failed to open to initialize bluetooth stack");
            q->error(error);
            _q_serviceDiscoveryFinished();
            return;
        }
    }
    m_btInitialized = true;
    errno = 0;
    bt_remote_device_t *remoteDevice = bt_rdev_get_device(address.toString().toLocal8Bit().constData());
    int deviceType = bt_rdev_get_type(remoteDevice);
    if (deviceType == -1) {
        qWarning() << "Could not retrieve remote device (address is 00:00:00:00:00:00).";
        error = QBluetoothServiceDiscoveryAgent::InputOutputError;
        errorString = QBluetoothServiceDiscoveryAgent::tr("Could not retrieve remote device (address is 00:00:00:00:00:00).");
        q->error(error);
        _q_serviceDiscoveryFinished();
        return;
    }
    /*
         * In case remote device is LE device, calling bt_rdev_sdp_search_async will cause memory fault.
         */

    if ( deviceType >1) {
        errno = 0;
        QPointer<QBluetoothServiceDiscoveryAgentPrivate> *classPointer = new QPointer<QBluetoothServiceDiscoveryAgentPrivate>(this);
        int b = bt_rdev_sdp_search_async(remoteDevice, 0, &(this->deviceServicesDiscoveryCallback), classPointer);
        if ( b != 0 ) {
            qWarning() << "Failed to run search on device: " << address.toString();
            error = QBluetoothServiceDiscoveryAgent::InputOutputError;
            errorString = QBluetoothServiceDiscoveryAgent::tr(strerror(errno));
            q->error(error);
            _q_serviceDiscoveryFinished();
            return;
        }
    }
    else
        _q_serviceDiscoveryFinished();
#else
    qBBBluetoothDebug() << "Starting Service discovery for" << address.toString();
    QByteArray filePath = QByteArray("/pps/services/bluetooth/remote_devices/").append(address.toString().toUtf8().constData());
    if ((m_rdfd = qt_safe_open(filePath.constData(), O_RDONLY)) == -1) {
        if (QFile::exists(filePath + QLatin1String("-00")) ||
            QFile::exists(filePath + QLatin1String("-01"))) {
            qBBBluetoothDebug() << "LE device discovered...skipping";
        } else {
            qWarning() << "Failed to open " << filePath;
            error = QBluetoothServiceDiscoveryAgent::InputOutputError;
            errorString = QStringLiteral("Failed to open remote device file");
            q->error(error);
        }
        _q_serviceDiscoveryFinished();
        return;
    } else {
        if (rdNotifier)
            delete rdNotifier;
        rdNotifier = new QSocketNotifier(m_rdfd, QSocketNotifier::Read, this);
        if (rdNotifier) {
            connect(rdNotifier, SIGNAL(activated(int)), this, SLOT(remoteDevicesChanged(int)));
        } else {
            qWarning() << "Service Discovery: Failed to connect to rdNotifier";
            error = QBluetoothServiceDiscoveryAgent::InputOutputError;
            errorString = QStringLiteral("Failed to connect to rdNotifier");
            q->error(error);
            _q_serviceDiscoveryFinished();
            return;
        }
    }
    m_queryTimer.start(10000);
    ppsSendControlMessage("service_query", QStringLiteral("{\"addr\":\"%1\"}").arg(address.toString()), this);
#endif
}

void QBluetoothServiceDiscoveryAgentPrivate::stop()
{
    m_queryTimer.stop();
    discoveredDevices.clear();
    setDiscoveryState(Inactive);
    Q_Q(QBluetoothServiceDiscoveryAgent);
    emit q->canceled();
    if (rdNotifier)
        delete rdNotifier;
    rdNotifier = 0;
    if (m_rdfd != -1) {
        qt_safe_close (m_rdfd);
        m_rdfd = -1;
    }
}

void QBluetoothServiceDiscoveryAgentPrivate::remoteDevicesChanged(int fd)
{
    if (discoveredDevices.count() == 0)
        return;
    pps_decoder_t ppsDecoder;
    pps_decoder_initialize(&ppsDecoder, 0);

    QBluetoothAddress deviceAddr;
    QString deviceName;

    if (!ppsReadRemoteDevice(fd, &ppsDecoder, &deviceAddr, &deviceName)) {
        pps_decoder_cleanup(&ppsDecoder);
        return;
    }

    pps_decoder_push(&ppsDecoder, "available_services");

    const char *next_service = 0;
    for (int service_count=0; pps_decoder_get_string(&ppsDecoder, 0, &next_service ) == PPS_DECODER_OK; service_count++) {
        if (next_service == 0)
            break;

        qBBBluetoothDebug() << Q_FUNC_INFO << "Service" << next_service;

        QBluetoothServiceInfo serviceInfo;
        serviceInfo.setDevice(discoveredDevices.at(0));

        QBluetoothServiceInfo::Sequence  protocolDescriptorList;
        protocolDescriptorList << QVariant::fromValue(QBluetoothUuid(QBluetoothUuid::L2cap));

        bool ok;
        QBluetoothUuid suuid(QByteArray(next_service).toUInt(&ok,16));
        if (!ok) {
            QList<QByteArray> serviceName = QByteArray(next_service).split(':');
            if (serviceName.size() == 2) {
                serviceInfo.setServiceUuid(QBluetoothUuid(QLatin1String(serviceName.last())));
                suuid = QBluetoothUuid((quint16)(serviceName.first().toUInt(&ok,16)));
                if (suuid == QBluetoothUuid::SerialPort) {
                    QBluetoothServiceInfo::Sequence protocol;
                    protocol << QVariant::fromValue(QBluetoothUuid(QBluetoothUuid::Rfcomm))
                             << QVariant::fromValue(0);
                    protocolDescriptorList.append(QVariant::fromValue(protocol));
                }
            }
        } else {
            //We do not have anything better, so we set the service class UUID as service UUID
            serviceInfo.setServiceUuid(suuid);
        }

        //Check if the UUID is in the uuidFilter
        if (!uuidFilter.isEmpty() && !uuidFilter.contains(serviceInfo.serviceUuid()))
            continue;

        serviceInfo.setAttribute(QBluetoothServiceInfo::ProtocolDescriptorList, protocolDescriptorList);

        QList<QBluetoothUuid> serviceClassId;
        serviceClassId << suuid;
        serviceInfo.setAttribute(QBluetoothServiceInfo::ServiceClassIds, QVariant::fromValue(serviceClassId));

        serviceInfo.setAttribute(QBluetoothServiceInfo::BrowseGroupList,
                                     QBluetoothUuid(QBluetoothUuid::PublicBrowseGroup));

        bool entryExists = false;
        //Did we already discover this service?
        foreach (QBluetoothServiceInfo sInfo, q_ptr->discoveredServices()) {
            if (sInfo.device() == serviceInfo.device()
                    && sInfo.serviceUuid() == serviceInfo.serviceUuid()
                    && sInfo.serviceClassUuids() == serviceInfo.serviceClassUuids()) {
                entryExists = true;
                //qBBBluetoothDebug() << "Entry exists" << serviceInfo.serviceClassUuids().first() << sInfo.serviceClassUuids().first();
                break;
            }
        }

        if (!entryExists) {
            qBBBluetoothDebug() << "Adding service" << next_service << " " << serviceInfo.socketProtocol();
            discoveredServices << serviceInfo;
            q_ptr->serviceDiscovered(serviceInfo);
        }
    }

    pps_decoder_cleanup(&ppsDecoder);
}

void QBluetoothServiceDiscoveryAgentPrivate::controlReply(ppsResult result)
{
    qBBBluetoothDebug() << "Control reply" << result.msg << result.dat;
    if (!m_queryTimer.isActive())
        return;
    m_queryTimer.stop();
    Q_Q(QBluetoothServiceDiscoveryAgent);
    if (!result.errorMsg.isEmpty()) {
        qWarning() << Q_FUNC_INFO << result.errorMsg;
        errorString = result.errorMsg;
        error = QBluetoothServiceDiscoveryAgent::InputOutputError;
        q->error(error);
    } else {
        _q_serviceDiscoveryFinished();
    }
}

void QBluetoothServiceDiscoveryAgentPrivate::controlEvent(ppsResult result)
{
    qBBBluetoothDebug() << "Control event" << result.msg << result.dat;
    if (!m_queryTimer.isActive())
        return;
    m_queryTimer.stop();
    Q_Q(QBluetoothServiceDiscoveryAgent);
    if (!result.errorMsg.isEmpty()) {
        qWarning() << Q_FUNC_INFO << result.errorMsg;
        errorString = result.errorMsg;
        error = QBluetoothServiceDiscoveryAgent::InputOutputError;
        q->error(error);
    } else {
        _q_serviceDiscoveryFinished();
    }
}

void QBluetoothServiceDiscoveryAgentPrivate::queryTimeout()
{
    Q_Q(QBluetoothServiceDiscoveryAgent);
    error = QBluetoothServiceDiscoveryAgent::UnknownError;
    errorString = QStringLiteral("Service query timed out");
    q->error(error);
    _q_serviceDiscoveryFinished();
}
QT_END_NAMESPACE
