/****************************************************************************************
 * Copyright (c) 2010 Patrick von Reth <patrick.vonreth@gmail.com>                      *
 *                                                                                      *
 * This program is free software; you can redistribute it and/or modify it under        *
 * the terms of the GNU General Public License as published by the Free Software        *
 * Foundation; either version 2 of the License, or (at your option) any later           *
 * version.                                                                             *
 *                                                                                      *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY      *
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A      *
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.             *
 *                                                                                      *
 * You should have received a copy of the GNU General Public License along with         *
 * this program.  If not, see <http://www.gnu.org/licenses/>.                           *
 ****************************************************************************************/

#include "freedesktopnotificationfrontend.h"
#include "notificationsadaptor.h"

#include "plugins/freedesktopnotification/fredesktopnotification.h"
#include "core/snoreserver.h"

#include <QtCore>
#include <QtDBus>
#include <QImage>

Q_EXPORT_PLUGIN2(freedesktop_frontend,FreedesktopNotification_Frontend)

FreedesktopNotification_Frontend::FreedesktopNotification_Frontend(SnoreServer *snore):
Notification_Frontend("FreedesktopNotification_Frontend",snore)
{
    new  NotificationsAdaptor(this);
    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.registerService( "org.freedesktop.Notifications" );
    dbus.registerObject( "/org/freedesktop/Notifications", this );
}

FreedesktopNotification_Frontend::~FreedesktopNotification_Frontend(){
    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.unregisterService( "org.freedesktop.Notifications" );
}

void FreedesktopNotification_Frontend::actionInvoked(QSharedPointer<Notification>notification){
    emit ActionInvoked(notification->id(),QString::number(notification->actionInvoked()));
}

void FreedesktopNotification_Frontend::notificationClosed(QSharedPointer<Notification>notification){
    uint reason;
    switch(notification->actionInvoked()){
    case Notification::TIMED_OUT:
        reason=1;
        break;
    case Notification::ACTION_1:
    case Notification::ACTION_2:
    case Notification::ACTION_3:
        reason=2;
        break;
    default:
        reason=4;
    }

    qDebug()<<"Closinf Dbus notification"<<QString::number(notification->id());
    emit NotificationClosed(notification->id(),reason);
}

QString FreedesktopNotification_Frontend::getImagefromHint(const FreedesktopImageHint &img){
    QString filename=QString(SnoreServer::snoreTMP()).append(img.hash()).append(".png");
    if(QFile::exists(filename))
        return filename;
    img.toQImage().save(filename,"PNG");
    return filename;
}

uint FreedesktopNotification_Frontend::Notify(const QString &app_name, uint replaces_id,
                                              const QString &app_icon, const QString &summary, const QString &body,
                                              const QStringList &actions, const QVariantMap &hints, int timeout)
{
    qDebug()<<app_name<<summary<<body<<app_icon;
    QString icon;

    if(hints.contains("image_data")){
        FreedesktopImageHint image;
        hints["image_data"].value<QDBusArgument>()>>image;
        icon=getImagefromHint(image);
    }
    if(!snore()->aplications().contains(app_name)){
        Application *a = new Application(app_name,app_icon);
        a->addAlert(new Alert("DBus Alert","DBus Alert",app_icon));
        snore()->addApplication(a);
        snore()->applicationIsInitialized(a);
    }
    QSharedPointer<Notification> noti(new Notification(this,app_name,"DBus Alert",summary,body,icon,timeout==-1?Notification::DefaultTimeout:timeout/1000,replaces_id));
    return snore()->broadcastNotification(noti);
}



void FreedesktopNotification_Frontend::CloseNotification(uint id){
    QSharedPointer<Notification> n(new Notification(id));
    snore()->closeNotification(n);
}

QStringList FreedesktopNotification_Frontend::GetCapabilities()
{
    return QStringList()
            << "body"
            //            << "body-hyperlinks"
            //            << "body-markup"
            << "icon-static"
            << "actions"
            ;
}

QString FreedesktopNotification_Frontend::GetServerInformation(QString& vendor, QString& version, QString& specVersion)
{
    vendor = "Snore";
    version = "0.01";
    specVersion = "0";
    return "Snore";
}


#include "freedesktopnotificationfrontend.moc"
