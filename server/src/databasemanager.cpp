#include "databasemanager.h"

DatabaseManager& DatabaseManager::instance()
{
    static DatabaseManager instance;
    return instance;
}

DatabaseManager::DatabaseManager(QObject *parent) : QObject(parent)
{
    connectionName_="remote_support_connection";
    if(QSqlDatabase::contains(connectionName_))
    {
        db_ = QSqlDatabase::database(connectionName_);
    }
    else
    {
        db_ = QSqlDatabase::addDatabase("QSQLITE",connectionName_);
    }

    QString dbDirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dbDir(dbDirPath);
    if(!dbDir.exists())
    {
        dbDir.mkpath(".");
    }
    QString dbPath = dbDir.filePath("remote_support.db");
    db_.setDatabaseName(dbPath);
    qInfo()<<"[DB] Database path:"<<dbPath;
}

DatabaseManager::~DatabaseManager()
{
    if(db_.isOpen())
    {
        db_.close();
        qInfo()<<"[DB] Database closed.";
    }
}

bool DatabaseManager::initialize()
{
        if(!db_.open())
        {
            qCritical()<<"[DB] Failed to open database:"<<db_.lastError().text();
        }

        QSqlQuery query(db_);

    QString createUserTable = R"(
                              CREATE TABLE IF NOT EXISTS users (
                              id INTEGER PRIMARY KEY AUTOINCREMENT,
                              username TEXT UNIQUE NOT NULL,
                              password_hash TEXT NOT NULL,
                              email TEXT,
                              phone TEXT,
                              user_type INTEGER NOT NULL,
                              created_at DATETIME DEFAULT CURRENT_TIMESTAMP
                              )
                              )";

    if(!query.exec(createUserTable))
    {
        qCritical()<<"Failed to create users table:"<<query.lastError().text();
        return false;
    }

    QString createWorkOrderTable = R"(
                                   CREATE TABLE IF NOT EXISTS work_orders(
                                   id INTEGER PRIMARY KEY AUTOINCREMENT,
                                   ticket_id TEXT UNIQUE NOT NULL,
                                   creator_id INTEGER NOT NULL,
                                   status TEXT DEFAULT 'open',
                                   created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
                                   closed_at DATETIME,
                                   FOREIGN KEY (creator_id) REFERENCES users (id)
                                   )
                                   )";
    if(!query.exec(createWorkOrderTable))
    {
        qCritical()<<"Failed to create work_orders table:"<<query.lastError().text();
        return false;
    }
    qInfo() << "Database initialized successfully! Tables (users/work_orders) are ready.";
    return true;
}


bool DatabaseManager::validateUser(const QString &username, const QString &password,int userType)
{
    if(!db_.isOpen())
    {
        qCritical() << "[DB] Validate user failed: Database is NOT open!";
        return false;
    }

    QSqlQuery query(db_);
    query.prepare(R"(SELECT password_hash FROM users WHERE username = :username AND user_type = :user_type)");
    query.bindValue(":username",username);
    query.bindValue(":user_type",userType);

    if(!query.exec())
    {
        qCritical() << "[DB] Query user failed:" << query.lastError().text();
        return false;
    }
    if(!query.next())
    {
        qWarning() << "[DB] User not found: " << username;
        return false;
    }

    QString storeHash =query.value(0).toString();
    qDebug()<< "[DB] Stored hash for" <<username<<":"<<storeHash;

    QByteArray inputPasswordBytes = password.toUtf8();
    QByteArray inputHashBytes = QCryptographicHash::hash(inputPasswordBytes,QCryptographicHash::Sha256);
    QString inputHash = inputHashBytes.toHex();
    qDebug()<<"[DB] Input password hash:"<<inputHash;

    bool isPasswordCorrect = (storeHash == inputHash);
    if(isPasswordCorrect)
    {
        qInfo()<<"[DB] User validate SUCCESS: "<<username;
    }
    else
    {
        qWarning()<<"[DB] User validate FAILED: Wrong password for"<<username;
    }
    return isPasswordCorrect;
}

bool DatabaseManager::userExists(const QString &username)
{
    if(!db_.open())
    {
        qCritical()<<"[DB] Check user existence failed: Database not open!";
        return false;
    }
    QSqlQuery query(db_);
    query.prepare("SELECT id FROM users WHERE username = :username");
    query.bindValue(":username",username);

    if(!query.exec())
    {
        qCritical()<<"[DB] Check user existence query failed:"<<query.lastError().text();
        return false;
    }

    bool exists = query.next();
    qInfo()<<"[DB] User"<<username<<"exists:"<<exists;
    return exists;
}

bool DatabaseManager::addUser(const QString &username, const QString &password, const QString &email, const QString &phone, int userType)
{
    if(!db_.open())
    {
        qCritical()<<"[DB] Add user failed: Database not open!";
        return false;
    }
    if(userExists(username))
    {
        qWarning()<<"[DB] Add user failed: Username already exists ~"<<username;
        return false;
    }
    QByteArray passwordHash = QCryptographicHash::hash(password.toUtf8(),QCryptographicHash::Sha256);
    QString hashStr = passwordHash.toHex();

    QSqlQuery query(db_);
    query.prepare(R"(
                  INSERT INTO users (username,password_hash,email,phone.user_type)
                  VALUES(:username,:password_hash,:email,:phone,:user_type)
                  )");
    query.bindValue(":username",username);
    query.bindValue(":password_hash", hashStr);
    query.bindValue(":email", email);
    query.bindValue(":phone", phone);
    query.bindValue(":user_type", userType);

    if(!query.exec())
    {
        qCritical()<<"[DB] Add user failed:"<<query.lastError().text();
        return false;
    }
    qInfo()<<"[DB] User added successfully:"<<username;
    return true;
}
