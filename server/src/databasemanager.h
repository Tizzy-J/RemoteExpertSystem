#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QCryptographicHash>

class DatabaseManager:public QObject
{
    Q_OBJECT
public:
    static DatabaseManager& instance();

    DatabaseManager(const DatabaseManager&)=delete;
    DatabaseManager& operator=(const DatabaseManager&)=delete;\

    bool initialize();
    bool addUser(const QString &username,const QString &password,const QString &email,const QString &phone,int userType);
    bool validateUser(const QString &username,const QString &password,int userType);
    bool userExists(const QString &username);

private:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager();
    QSqlDatabase db_;
    QString connectionName_;
};

#endif // DATABASEMANAGER_H
