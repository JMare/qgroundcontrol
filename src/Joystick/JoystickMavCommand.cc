/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "JoystickMavCommand.h"
#include "QGCLoggingCategory.h"
#include "Vehicle.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonArray>

QGC_LOGGING_CATEGORY(JoystickMavCommandLog, "JoystickMavCommandLog")

static void parseJsonValue(const QJsonObject& jsonObject, const QString& key, float& param)
{
    if (jsonObject.contains(key))
        param = static_cast<float>(jsonObject.value(key).toDouble());
}

QList<JoystickMavCommand> JoystickMavCommand::load(const QString& jsonFilename)
{
    qCDebug(JoystickMavCommandLog) << "Loading" << jsonFilename;
    QList<JoystickMavCommand> result;

    QFile jsonFile(jsonFilename);
    if (!jsonFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCDebug(JoystickMavCommandLog) << "Could not open" << jsonFilename;
        return result;
    }

    QByteArray bytes = jsonFile.readAll();
    jsonFile.close();
    QJsonParseError jsonParseError;
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &jsonParseError);
    if (jsonParseError.error != QJsonParseError::NoError) {
        qWarning() << jsonFilename << "Unable to open json document" << jsonParseError.errorString();
        return result;
    }

    QJsonObject json = doc.object();

    const int version = json.value("version").toInt();
    if (version != 1) {
        qWarning() << jsonFilename << ": invalid version" << version;
        return result;
    }

    QJsonValue jsonValue = json.value("commands");
    if (!jsonValue.isArray()) {
        qWarning() << jsonFilename << ": 'commands' is not an array";
        return result;
    }

    QJsonArray jsonArray = jsonValue.toArray();
    for (QJsonValue info: jsonArray) {
        if (!info.isObject()) {
            qWarning() << jsonFilename << ": 'commands' should contain objects";
            return result;
        }

        auto jsonObject = info.toObject();
        JoystickMavCommand item;
        if (!jsonObject.contains("id")) {
            qWarning() << jsonFilename << ": 'id' is required";
            continue;
        }
        item._id = jsonObject.value("id").toInt();
        if (!jsonObject.contains("name")) {
            qWarning() << jsonFilename << ": 'name' is required";
            continue;
        }
        item._name = jsonObject.value("name").toString();

        // target comp id is optional, if not set, will use default component id
        if (!jsonObject.contains("targetcomponentid")) {
            item._specifiedtargetcomponent = false;
        }
        else
        {
            item._specifiedtargetcomponent = true;
            item._targetcomponentid = jsonObject.value("targetcomponentid").toInt();
        }
        item._showError = jsonObject.value("showError").toBool();
        parseJsonValue(jsonObject, "param1", item._param1);
        parseJsonValue(jsonObject, "param2", item._param2);
        parseJsonValue(jsonObject, "param3", item._param3);
        parseJsonValue(jsonObject, "param4", item._param4);
        parseJsonValue(jsonObject, "param5", item._param5);
        parseJsonValue(jsonObject, "param6", item._param6);
        parseJsonValue(jsonObject, "param7", item._param7);

        qCDebug(JoystickMavCommandLog) << jsonObject;

        result.append(item);
    }

    return result;
}

void JoystickMavCommand::send(Vehicle* vehicle)
{
    uint8_t target_component = vehicle->defaultComponentId();
    if(_specifiedtargetcomponent)
    {
        target_component = _targetcomponentid;
    }

    vehicle->sendMavCommand(target_component,
                            static_cast<MAV_CMD>(_id),
                            _showError,
                            _param1, _param2, _param3, _param4, _param5, _param6, _param7);
}
