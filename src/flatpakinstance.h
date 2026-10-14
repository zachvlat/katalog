#pragma once

#include <QObject>

struct _FlatpakInstallation;
typedef struct _FlatpakInstallation FlatpakInstallation;

class FlatpakManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasSystem READ hasSystem CONSTANT)
    Q_PROPERTY(bool hasUser READ hasUser CONSTANT)
public:
    explicit FlatpakManager(QObject *parent = nullptr);
    ~FlatpakManager() override;

    bool hasSystem() const;
    bool hasUser() const;

    FlatpakInstallation *systemInstallation() const;
    FlatpakInstallation *userInstallation() const;

private:
#ifdef HAVE_FLATPAK
    FlatpakInstallation *m_system = nullptr;
    FlatpakInstallation *m_user = nullptr;
#endif
};
