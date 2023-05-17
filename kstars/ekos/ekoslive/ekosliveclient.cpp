/*
    SPDX-FileCopyrightText: 2018 Jasem Mutlaq <mutlaqja@ikarustech.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "Options.h"

#include "ekosliveclient.h"
#include "ekos/manager.h"

#include "kspaths.h"
#include "QProgressIndicator.h"

#include <config-kstars.h>

#ifdef HAVE_KEYCHAIN
#include <qt5keychain/keychain.h>
#endif

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace EkosLive
{
Client::Client(Ekos::Manager *manager) : QDialog(manager), m_Manager(manager)
{
    setupUi(this);

    connect(closeB, &QPushButton::clicked, this, &Client::close);

    QPixmap im;
    if (im.load(KSPaths::locate(QStandardPaths::AppLocalDataLocation, "ekoslive.png")))
        leftBanner->setPixmap(im);

    pi = new QProgressIndicator(this);
    bottomLayout->insertWidget(1, pi);

    connectionState->setPixmap(QIcon::fromTheme("state-offline").pixmap(QSize(64, 64)));

    username->setText(Options::ekosLiveUsername());
    connect(username, &QLineEdit::editingFinished, this, [this]()
    {
        Options::setEkosLiveUsername(username->text());
    });

    // Initialize node managers
    QSharedPointer<NodeManager> onlineManager(new NodeManager(QUrl("https://live.stellarmate.com"), QUrl("wss://live.stellarmate.com")));
    connect(onlineManager.get(), &NodeManager::authenticationError, this, [this](const QString & message)
    {
        onlineLabel->setToolTip(message);
    });

    QSharedPointer<NodeManager> offlineManager(new NodeManager(QUrl("http://localhost:3000"), QUrl("ws://localhost:3000")));
    connect(offlineManager.get(), &NodeManager::authenticationError, this, [this](const QString & message)
    {
        offlineLabel->setToolTip(message);
    });

    m_NodeManagers.append(std::move(onlineManager));
    m_NodeManagers.append(std::move(offlineManager));

    connect(connectB, &QPushButton::clicked, this, [this]()
    {
        if (m_isConnected)
        {
            for (auto &oneManager : m_NodeManagers)
                oneManager->disconnectNodes();
        }
        else
        {
            for (auto &oneManager : m_NodeManagers)
            {
                oneManager->setCredentials(username->text(), password->text());
                oneManager->authenticate();
            }
        }
    });

    connect(password, &QLineEdit::returnPressed, this, [this]()
    {
        if (!m_isConnected)
        {
            for (auto &oneManager : m_NodeManagers)
            {
                oneManager->setCredentials(username->text(), password->text());
                oneManager->authenticate();
            }
        }
    });

    rememberCredentialsCheck->setChecked(Options::rememberCredentials());
    connect(rememberCredentialsCheck, &QCheckBox::toggled, [ = ](bool toggled)
    {
        Options::setRememberCredentials(toggled);
    });
    autoStartCheck->setChecked(Options::autoStartEkosLive());
    connect(autoStartCheck, &QCheckBox::toggled, [ = ](bool toggled)
    {
        Options::setAutoStartEkosLive(toggled);
    });

#ifdef HAVE_KEYCHAIN
    QKeychain::ReadPasswordJob *job = new QKeychain::ReadPasswordJob(QLatin1String("kstars"));
    job->setAutoDelete(false);
    job->setKey(QLatin1String("ekoslive"));
    connect(job, &QKeychain::Job::finished, this, [&](QKeychain::Job * job)
    {
        if (job->error() == false)
        {
            const auto passwordText = dynamic_cast<QKeychain::ReadPasswordJob*>(job)->textData().toLatin1();

            // Only set and attempt connection if the data is not empty
            if (passwordText.isEmpty() == false && username->text().isEmpty() == false)
            {
                password->setText(passwordText);
                if (autoStartCheck->isChecked())
                {
                    for (auto &oneManager : m_NodeManagers)
                    {
                        oneManager->setCredentials(username->text(), password->text());
                        oneManager->authenticate();
                    }
                }
            }

        }
        job->deleteLater();
    });
    job->start();
#endif

    m_Message = new Message(m_Manager, m_NodeManagers);
    connect(m_Message, &Message::connected, this, &Client::onConnected);
    connect(m_Message, &Message::disconnected, this, &Client::onDisconnected);
    connect(m_Message, &Message::expired, this, [&](const QUrl & url)
    {
        // If token expired, disconnect and reconnect again.
        for (auto &oneManager : m_NodeManagers)
        {
            if (oneManager->wsURL() == url)
            {
                oneManager->disconnectNodes();
                oneManager->setCredentials(username->text(), password->text());
                oneManager->authenticate();
            }
        }
    });

    m_Media = new Media(m_Manager, m_NodeManagers);
    m_Cloud = new Cloud(m_Manager, m_NodeManagers);
}

Client::~Client()
{
    for (auto &oneManager : m_NodeManagers)
        oneManager->disconnectNodes();
}

void Client::onConnected()
{
    pi->stopAnimation();

    m_isConnected = true;

    connectB->setText(i18n("Disconnect"));
    connectionState->setPixmap(QIcon::fromTheme("state-ok").pixmap(QSize(64, 64)));

    auto disconnected = QIcon(":/icons/AlignFailure.svg").pixmap(QSize(32, 32));
    auto connected = QIcon(":/icons/AlignSuccess.svg").pixmap(QSize(32, 32));

    onlineLabel->setStyleSheet(m_NodeManagers[0]->isConnected() ? "color:white" : "color:gray");
    onlineIcon->setPixmap(m_NodeManagers[0]->isConnected() ? connected : disconnected);
    if (m_NodeManagers[0]->isConnected())
        onlineLabel->setToolTip(QString());

    offlineLabel->setStyleSheet(m_NodeManagers[1]->isConnected() ? "color:white" : "color:gray");
    offlineIcon->setPixmap(m_NodeManagers[1]->isConnected() ? connected : disconnected);
    if (m_NodeManagers[1]->isConnected())
        offlineLabel->setToolTip(QString());

    if (rememberCredentialsCheck->isChecked())
    {
#ifdef HAVE_KEYCHAIN
        QKeychain::WritePasswordJob *job = new QKeychain::WritePasswordJob(QLatin1String("kstars"));
        job->setAutoDelete(true);
        job->setKey(QLatin1String("ekoslive"));
        job->setTextData(password->text());
        job->start();
#endif
    }
}

void Client::onDisconnected()
{
    connectionState->setPixmap(QIcon::fromTheme("state-offline").pixmap(QSize(64, 64)));
    m_isConnected = false;
    connectB->setText(i18n("Connect"));

    auto disconnected = QIcon(":/icons/AlignFailure.svg").pixmap(QSize(32, 32));
    auto connected = QIcon(":/icons/AlignSuccess.svg").pixmap(QSize(32, 32));

    onlineLabel->setStyleSheet(m_NodeManagers[0]->isConnected() ? "color:white" : "color:gray");
    onlineIcon->setPixmap(m_NodeManagers[0]->isConnected() ? connected : disconnected);

    offlineLabel->setStyleSheet(m_NodeManagers[1]->isConnected() ? "color:white" : "color:gray");
    offlineIcon->setPixmap(m_NodeManagers[1]->isConnected() ? connected : disconnected);
}

void Client::setConnected(bool enabled)
{
    // Return if there is no change.
    if (enabled == m_isConnected)
        return;

    connectB->click();
}

void Client::setConfig(bool rememberCredentials, bool autoConnect)
{
    rememberCredentialsCheck->setChecked(rememberCredentials);
    autoStartCheck->setChecked(autoConnect);
}

void Client::setUser(const QString &user, const QString &pass)
{
    username->setText(user);
    Options::setEkosLiveUsername(user);

    password->setText(pass);
}

}
