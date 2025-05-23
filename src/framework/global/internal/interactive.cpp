/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "interactive.h"

#include <QUrl>

#include <QMainWindow>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QMap>
#include <QSpacerItem>
#include <QGridLayout>
#include <QDesktopServices>

#ifdef Q_OS_MAC
#include "platform/macos/macosinteractivehelper.h"
#elif defined(Q_OS_WIN)
#include <QDir>
#include <QProcess>
#include "platform/win/wininteractivehelper.h"
#endif

#include "translation.h"
#include "io/path.h"

#include "log.h"

using namespace muse;

static IInteractive::Result standardDialogResult(const RetVal<Val>& retVal)
{
    if (!retVal.ret) {
        return IInteractive::Result(static_cast<int>(IInteractive::Button::Cancel));
    }

    QVariantMap resultMap = retVal.val.toQVariant().toMap();

    int btn = resultMap["buttonId"].toInt();
    bool showAgain = resultMap["showAgain"].toBool();
    return IInteractive::Result(btn, showAgain);
}

#ifndef Q_OS_LINUX
static QString filterToString(const std::vector<std::string>& filter)
{
    QStringList result;
    for (const std::string& nameFilter : filter) {
        result << QString::fromStdString(nameFilter);
    }

    return result.join(";;");
}

#endif

IInteractive::ButtonData Interactive::buttonData(Button b) const
{
    constexpr bool accent = true;

    switch (b) {
    case IInteractive::Button::NoButton:    return ButtonData(int(b), "");
    case IInteractive::Button::Ok:          return ButtonData(int(b), muse::trc("global", "OK"), accent);
    case IInteractive::Button::Save:        return ButtonData(int(b), muse::trc("global", "Save"), accent);
    case IInteractive::Button::SaveAll:     return ButtonData(int(b), muse::trc("global", "Save all"));
    case IInteractive::Button::DontSave:    return ButtonData(int(b), muse::trc("global", "Don’t save"));
    case IInteractive::Button::Open:        return ButtonData(int(b), muse::trc("global", "Open"));
    case IInteractive::Button::Yes:         return ButtonData(int(b), muse::trc("global", "Yes"), accent);
    case IInteractive::Button::YesToAll:    return ButtonData(int(b), muse::trc("global", "Yes to all"), accent);
    case IInteractive::Button::No:          return ButtonData(int(b), muse::trc("global", "No"));
    case IInteractive::Button::NoToAll:     return ButtonData(int(b), muse::trc("global", "No to all"));
    case IInteractive::Button::Abort:       return ButtonData(int(b), muse::trc("global", "Abort"));
    case IInteractive::Button::Retry:       return ButtonData(int(b), muse::trc("global", "Retry"));
    case IInteractive::Button::Ignore:      return ButtonData(int(b), muse::trc("global", "Ignore"));
    case IInteractive::Button::Close:       return ButtonData(int(b), muse::trc("global", "Close"));
    case IInteractive::Button::Cancel:      return ButtonData(int(b), muse::trc("global", "Cancel"));
    case IInteractive::Button::Discard:     return ButtonData(int(b), muse::trc("global", "Discard"));
    case IInteractive::Button::Help:        return ButtonData(int(b), muse::trc("global", "Help"));
    case IInteractive::Button::Apply:       return ButtonData(int(b), muse::trc("global", "Apply"));
    case IInteractive::Button::Reset:       return ButtonData(int(b), muse::trc("global", "Reset"));
    case IInteractive::Button::Continue:    return ButtonData(int(b), muse::trc("global", "Continue"));
    case IInteractive::Button::Next:
    case IInteractive::Button::Back:
    case IInteractive::Button::Select:
    case IInteractive::Button::Clear:
    case IInteractive::Button::Done:
    case IInteractive::Button::RestoreDefaults:
    case IInteractive::Button::CustomButton: break;
    }

    return ButtonData(int(b), "");
}

async::Promise<IInteractive::Result> Interactive::openStandartAsync(const std::string& type, const std::string& contentTitle,
                                                                    const Text& text,
                                                                    const ButtonDatas& buttons, int defBtn,
                                                                    const Options& options, const std::string& dialogTitle)
{
    auto format = [](IInteractive::TextFormat f) {
        switch (f) {
        case IInteractive::TextFormat::Auto:      return Qt::AutoText;
        case IInteractive::TextFormat::PlainText: return Qt::PlainText;
        case IInteractive::TextFormat::RichText:  return Qt::RichText;
        }
        return Qt::PlainText;
    };

    UriQuery q("muse://interactive/standard");
    q.add("type", type)
    .add("contentTitle", contentTitle)
    .add("text", text.text)
    .add("textFormat", (int)format(text.format))
    .add("defaultButtonId", defBtn)
    .add("withIcon", options.testFlag(IInteractive::Option::WithIcon))
    .add("withDontShowAgainCheckBox", options.testFlag(IInteractive::Option::WithDontShowAgainCheckBox))
    .add("dialogTitle", dialogTitle);

    ValList buttonsList;
    ValList customButtonsList;
    if (buttons.empty()) {
        buttonsList.push_back(Val(static_cast<int>(IInteractive::Button::Ok)));
    } else {
        for (const IInteractive::ButtonData& buttonData: buttons) {
            ValMap customButton;
            customButton["text"] = Val(buttonData.text);
            customButton["buttonId"] = Val(buttonData.btn);
            customButton["role"] = Val(static_cast<int>(buttonData.role));
            customButton["isAccent"] = Val(buttonData.accent);
            customButton["isLeftSide"] = Val(buttonData.leftSide);
            customButtonsList.push_back(Val(customButton));
        }
    }

    q.add("buttons", Val(buttonsList))
    .add("customButtons", Val(customButtonsList));

    async::Promise<Val> promise = provider()->openAsync(q);

    return async::make_promise<Result>([promise, this](auto resolve, auto reject) {
        async::Promise<Val> mut = promise;
        mut.onResolve(this, [resolve](const Val& val) {
            QVariantMap resultMap = val.toQVariant().toMap();
            int btn = resultMap["buttonId"].toInt();
            bool showAgain = resultMap["showAgain"].toBool();
            (void)resolve(IInteractive::Result(btn, showAgain));
        }).onReject(this, [resolve, reject](int code, const std::string& err) {
            //! NOTE To simplify writing the handler
            (void)resolve(IInteractive::Result((int)IInteractive::Button::Cancel, false));
            (void)reject(code, err);
        });
        return async::Promise<IInteractive::Result>::Result::unchecked();
    });
}

IInteractive::Result Interactive::question(const std::string& contentTitle, const std::string& text,
                                           const Buttons& buttons,
                                           const Button& def,
                                           const Options& options,
                                           const std::string& dialogTitle) const
{
    return question(contentTitle, Text(text), buttonDataList(buttons), int(def), options, dialogTitle);
}

IInteractive::Result Interactive::question(const std::string& contentTitle, const Text& text, const ButtonDatas& btns, int defBtn,
                                           const Options& options, const std::string& dialogTitle) const
{
    return standardDialogResult(provider()->question(contentTitle, text, btns, defBtn, options, dialogTitle));
}

async::Promise<IInteractive::Result> Interactive::questionAsync(const std::string& contentTitle, const Text& text,
                                                                const ButtonDatas& buttons, int defBtn,
                                                                const Options& options, const std::string& dialogTitle)
{
    return openStandartAsync("QUESTION", contentTitle, text, buttons, defBtn, options, dialogTitle);
}

IInteractive::Result Interactive::info(const std::string& contentTitle, const std::string& text, const Buttons& buttons,
                                       int defBtn,
                                       const Options& options,
                                       const std::string& dialogTitle) const
{
    return standardDialogResult(provider()->info(contentTitle, text, buttonDataList(buttons), defBtn, options, dialogTitle));
}

IInteractive::Result Interactive::info(const std::string& contentTitle, const Text& text, const ButtonDatas& buttons, int defBtn,
                                       const Options& options, const std::string& dialogTitle) const
{
    return standardDialogResult(provider()->info(contentTitle, text, buttons, defBtn, options, dialogTitle));
}

Interactive::Result Interactive::warning(const std::string& contentTitle, const std::string& text, const Buttons& buttons,
                                         const Button& defBtn, const Options& options, const std::string& dialogTitle) const
{
    return standardDialogResult(provider()->warning(contentTitle, text, {}, buttonDataList(buttons), int(defBtn), options, dialogTitle));
}

IInteractive::Result Interactive::warning(const std::string& contentTitle, const Text& text, const ButtonDatas& buttons,
                                          int defBtn,
                                          const Options& options,
                                          const std::string& dialogTitle) const
{
    return standardDialogResult(provider()->warning(contentTitle, text, {}, buttons, defBtn, options, dialogTitle));
}

IInteractive::Result Interactive::warning(const std::string& contentTitle, const Text& text, const std::string& detailedText,
                                          const ButtonDatas& buttons, int defBtn,
                                          const Options& options, const std::string& dialogTitle) const
{
    return standardDialogResult(provider()->warning(contentTitle, text, detailedText, buttons, defBtn, options, dialogTitle));
}

IInteractive::Result Interactive::error(const std::string& contentTitle, const std::string& text,
                                        const Buttons& buttons, const Button& defBtn,
                                        const Options& options, const std::string& dialogTitle) const
{
    return standardDialogResult(provider()->error(contentTitle, text, {}, buttonDataList(buttons), int(defBtn), options, dialogTitle));
}

IInteractive::Result Interactive::error(const std::string& contentTitle, const Text& text,
                                        const ButtonDatas& buttons, int defBtn,
                                        const Options& options, const std::string& dialogTitle) const
{
    return standardDialogResult(provider()->error(contentTitle, text, {}, buttons, defBtn, options, dialogTitle));
}

IInteractive::Result Interactive::error(const std::string& contentTitle, const Text& text, const std::string& detailedText,
                                        const ButtonDatas& buttons, int defBtn,
                                        const Options& options, const std::string& dialogTitle) const
{
    return standardDialogResult(provider()->error(contentTitle, text, detailedText, buttons, defBtn, options, dialogTitle));
}

Ret Interactive::showProgress(const std::string& title, Progress* progress) const
{
    return provider()->showProgress(title, progress);
}

io::path_t Interactive::selectOpeningFile(const QString& title, const io::path_t& dir, const std::vector<std::string>& filter)
{
#ifndef Q_OS_LINUX
    QString result = QFileDialog::getOpenFileName(nullptr, title, dir.toQString(), filterToString(filter));
    return result;
#else
    return provider()->selectOpeningFile(title.toStdString(), dir, filter).val;
#endif
}

io::path_t Interactive::selectSavingFile(const QString& title, const io::path_t& path, const std::vector<std::string>& filter,
                                         bool confirmOverwrite)
{
#ifndef Q_OS_LINUX
    QFileDialog::Options options;
    options.setFlag(QFileDialog::DontConfirmOverwrite, !confirmOverwrite);
    QString result = QFileDialog::getSaveFileName(nullptr, title, path.toQString(), filterToString(filter), nullptr, options);
    return result;
#else
    return provider()->selectSavingFile(title.toStdString(), path, filter, confirmOverwrite).val;
#endif
}

io::path_t Interactive::selectDirectory(const QString& title, const io::path_t& dir)
{
#ifndef Q_OS_LINUX
    QString result = QFileDialog::getExistingDirectory(nullptr, title, dir.toQString());
    return result;
#else
    return provider()->selectDirectory(title.toStdString(), dir).val;
#endif
}

io::paths_t Interactive::selectMultipleDirectories(const QString& title, const io::path_t& dir, const io::paths_t& selectedDirectories)
{
    QString directoriesStr = QString::fromStdString(io::pathsToString(selectedDirectories));
    QStringList params = {
        "title=" + title,
        "selectedDirectories=" + directoriesStr,
        "startDir=" + dir.toQString()
    };

    RetVal<Val> result = open("muse://interactive/selectmultipledirectories?" + params.join("&").toStdString());
    if (!result.ret) {
        return selectedDirectories;
    }

    return io::pathsFromString(result.val.toQString().toStdString());
}

QColor Interactive::selectColor(const QColor& color, const QString& title)
{
    shortcutsRegister()->setActive(false);
    QColor selectColor = provider()->selectColor(color, title).val;
    shortcutsRegister()->setActive(true);
    return selectColor;
}

bool Interactive::isSelectColorOpened() const
{
    return provider()->isSelectColorOpened();
}

RetVal<Val> Interactive::open(const std::string& uri) const
{
    return open(UriQuery(uri));
}

RetVal<Val> Interactive::open(const Uri& uri) const
{
    return open(UriQuery(uri));
}

RetVal<Val> Interactive::open(const UriQuery& uri) const
{
    UriQuery newQuery = uri;
    if (!newQuery.contains("sync")) {
        newQuery.addParam("sync", Val(true));
    }

    return provider()->open(newQuery);
}

async::Promise<Val> Interactive::openAsync(const UriQuery& uri)
{
    return provider()->openAsync(uri);
}

RetVal<bool> Interactive::isOpened(const std::string& uri) const
{
    return provider()->isOpened(Uri(uri));
}

RetVal<bool> Interactive::isOpened(const Uri& uri) const
{
    return provider()->isOpened(uri);
}

RetVal<bool> Interactive::isOpened(const UriQuery& uri) const
{
    return provider()->isOpened(uri);
}

async::Channel<Uri> Interactive::opened() const
{
    return provider()->opened();
}

void Interactive::raise(const UriQuery& uri)
{
    provider()->raise(uri);
}

void Interactive::close(const std::string& uri)
{
    provider()->close(Uri(uri));
}

void Interactive::close(const Uri& uri)
{
    provider()->close(uri);
}

void Interactive::close(const UriQuery& uri)
{
    provider()->close(uri);
}

void Interactive::closeAllDialogs()
{
    provider()->closeAllDialogs();
}

ValCh<Uri> Interactive::currentUri() const
{
    return provider()->currentUri();
}

RetVal<bool> Interactive::isCurrentUriDialog() const
{
    return provider()->isCurrentUriDialog();
}

std::vector<Uri> Interactive::stack() const
{
    return provider()->stack();
}

Ret Interactive::openUrl(const std::string& url) const
{
    return openUrl(QUrl(QString::fromStdString(url)));
}

Ret Interactive::openUrl(const QUrl& url) const
{
    return QDesktopServices::openUrl(url);
}

Ret Interactive::isAppExists(const std::string& appIdentifier) const
{
#ifdef Q_OS_MACOS
    return MacOSInteractiveHelper::isAppExists(appIdentifier);
#else
    NOT_IMPLEMENTED;
    UNUSED(appIdentifier);
    return false;
#endif
}

Ret Interactive::canOpenApp(const Uri& uri) const
{
#ifdef Q_OS_MACOS
    return MacOSInteractiveHelper::canOpenApp(uri);
#else
    NOT_IMPLEMENTED;
    UNUSED(uri);
    return false;
#endif
}

async::Promise<Ret> Interactive::openApp(const Uri& uri) const
{
#ifdef Q_OS_MACOS
    return MacOSInteractiveHelper::openApp(uri);
#elif defined(Q_OS_WIN)
    return WinInteractiveHelper::openApp(uri);
#else
    UNUSED(uri);
    return async::Promise<Ret>([](auto, auto reject) {
        Ret ret = make_ret(Ret::Code::NotImplemented);
        return reject(ret.code(), ret.text());
    });
#endif
}

Ret Interactive::revealInFileBrowser(const io::path_t& filePath) const
{
#ifdef Q_OS_MACOS
    if (MacOSInteractiveHelper::revealInFinder(filePath)) {
        return true;
    }
#elif defined(Q_OS_WIN)
    QString command = QLatin1String("explorer /select,%1").arg(QDir::toNativeSeparators(filePath.toQString()));
    if (QProcess::startDetached(command, QStringList())) {
        return true;
    }
#endif
    io::path_t dirPath = io::dirpath(filePath);
    return openUrl(QUrl::fromLocalFile(dirPath.toQString()));
}
