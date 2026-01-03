/*
 Copyright (C) 2010 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Localization.h"

#include "io/PathQt.h"
#include "io/SystemPaths.h"

#include <QLibraryInfo>
#include <QTranslator>

namespace tb::ui
{
namespace
{

QString normalizeId(QString id)
{
  id = id.trimmed();
  return id.replace('-', '_');
}

const std::vector<LanguageDefinition>& languageDefinitions()
{
  static const auto languages = std::vector<LanguageDefinition>{
    {QStringLiteral("en"), QStringLiteral("English"), QLocale{QLocale::English}},
    {QStringLiteral("fr"), QStringLiteral("French"), QLocale{QLocale::French}},
    {QStringLiteral("pl"), QStringLiteral("Polish"), QLocale{QLocale::Polish}},
    {QStringLiteral("de"), QStringLiteral("German"), QLocale{QLocale::German}},
    {QStringLiteral("es"), QStringLiteral("Spanish"), QLocale{QLocale::Spanish}},
    {QStringLiteral("it"), QStringLiteral("Italian"), QLocale{QLocale::Italian}},
    {QStringLiteral("pt_BR"), QStringLiteral("Portuguese (Brazil)"),
     QLocale{QLocale::Portuguese, QLocale::Brazil}},
    {QStringLiteral("ru"), QStringLiteral("Russian"), QLocale{QLocale::Russian}},
    {QStringLiteral("ja"), QStringLiteral("Japanese"), QLocale{QLocale::Japanese}},
    {QStringLiteral("ko"), QStringLiteral("Korean"), QLocale{QLocale::Korean}},
    {QStringLiteral("zh_CN"), QStringLiteral("Chinese (Simplified)"),
     QLocale{QLocale::Chinese, QLocale::China}},
    {QStringLiteral("zh_TW"), QStringLiteral("Chinese (Traditional)"),
     QLocale{QLocale::Chinese, QLocale::Taiwan}},
    {QStringLiteral("cs"), QStringLiteral("Czech"), QLocale{QLocale::Czech}},
    {QStringLiteral("nl"), QStringLiteral("Dutch"), QLocale{QLocale::Dutch}},
    {QStringLiteral("sv"), QStringLiteral("Swedish"), QLocale{QLocale::Swedish}},
    {QStringLiteral("nb"), QStringLiteral("Norwegian Bokmal"),
     QLocale{QLocale::NorwegianBokmal}},
    {QStringLiteral("da"), QStringLiteral("Danish"), QLocale{QLocale::Danish}},
    {QStringLiteral("fi"), QStringLiteral("Finnish"), QLocale{QLocale::Finnish}},
    {QStringLiteral("tr"), QStringLiteral("Turkish"), QLocale{QLocale::Turkish}},
    {QStringLiteral("uk"), QStringLiteral("Ukrainian"), QLocale{QLocale::Ukrainian}},
  };
  return languages;
}

const LanguageDefinition* findLanguage(const QString& id)
{
  const auto normalized = normalizeId(id);
  for (const auto& language : languageDefinitions())
  {
    if (normalizeId(language.id).compare(normalized, Qt::CaseInsensitive) == 0)
    {
      return &language;
    }

    if (
      normalizeId(language.locale.name()).compare(normalized, Qt::CaseInsensitive) == 0)
    {
      return &language;
    }
  }

  return nullptr;
}

const LanguageDefinition* matchLocale(const QLocale& locale)
{
  const auto localeName = normalizeId(locale.name());
  for (const auto& language : languageDefinitions())
  {
    if (normalizeId(language.id).compare(localeName, Qt::CaseInsensitive) == 0)
    {
      return &language;
    }
  }

  for (const auto& language : languageDefinitions())
  {
    if (
      language.locale.language() == locale.language()
      && language.locale.country() == locale.country())
    {
      return &language;
    }
  }

  for (const auto& language : languageDefinitions())
  {
    if (language.locale.language() == locale.language())
    {
      return &language;
    }
  }

  return nullptr;
}

QStringList uniqueCandidates(const QStringList& values)
{
  auto result = QStringList{};
  for (const auto& value : values)
  {
    if (!value.isEmpty() && !result.contains(value, Qt::CaseInsensitive))
    {
      result.push_back(value);
    }
  }
  return result;
}

} // namespace

const std::vector<LanguageDefinition>& supportedLanguages()
{
  return languageDefinitions();
}

QString systemLanguageId()
{
  return QStringLiteral("system");
}

QString defaultLanguageId()
{
  return QStringLiteral("en");
}

QString resolveLanguageId(const QString& preferenceValue)
{
  const auto normalized = normalizeId(preferenceValue);
  if (normalized.isEmpty() || normalized.compare(systemLanguageId(), Qt::CaseInsensitive) == 0)
  {
    const auto systemLocale = QLocale::system();
    const auto uiLanguages = systemLocale.uiLanguages();
    for (const auto& tag : uiLanguages)
    {
      if (const auto* match = matchLocale(QLocale{tag}))
      {
        return match->id;
      }
    }

    if (const auto* match = matchLocale(systemLocale))
    {
      return match->id;
    }

    return defaultLanguageId();
  }

  if (const auto* language = findLanguage(normalized))
  {
    return language->id;
  }

  return defaultLanguageId();
}

QLocale localeForLanguageId(const QString& languageId)
{
  if (const auto* language = findLanguage(languageId))
  {
    return language->locale;
  }

  return QLocale{QLocale::English};
}

QStringList translationCandidates(const QString& languageId)
{
  if (languageId.isEmpty())
  {
    return {};
  }

  auto candidates = QStringList{};
  candidates << normalizeId(languageId);

  const auto locale = localeForLanguageId(languageId);
  const auto localeName = normalizeId(locale.name());
  candidates << localeName;
  candidates << localeName.split('_').front();

  return uniqueCandidates(candidates);
}

bool loadAppTranslation(QTranslator& translator, const QString& languageId)
{
  const auto candidates = translationCandidates(languageId);
  if (candidates.isEmpty())
  {
    return false;
  }

  const auto directories = io::SystemPaths::findResourceDirectories("translations");
  for (const auto& directory : directories)
  {
    const auto qDirectory = io::pathAsQPath(directory);
    for (const auto& candidate : candidates)
    {
      const auto fileName = QStringLiteral("brumschtick_%1").arg(candidate);
      if (translator.load(fileName, qDirectory))
      {
        return true;
      }
    }
  }

  return false;
}

bool loadQtTranslation(QTranslator& translator, const QString& languageId)
{
  const auto locale = localeForLanguageId(languageId);
  const auto translationsPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
  if (translator.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"), translationsPath))
  {
    return true;
  }
  return translator.load(locale, QStringLiteral("qt"), QStringLiteral("_"), translationsPath);
}

} // namespace tb::ui
