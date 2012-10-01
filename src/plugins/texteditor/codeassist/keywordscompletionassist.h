/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2012 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: http://www.qt-project.org/
**
**
** GNU Lesser General Public License Usage
**
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this file.
** Please review the following information to ensure the GNU Lesser General
** Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** Other Usage
**
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**************************************************************************/

#ifndef KEYWORDSCOMPLETIONASSIST_H
#define KEYWORDSCOMPLETIONASSIST_H

#include "iassistprocessor.h"
#include "basicproposalitem.h"
#include "ifunctionhintproposalmodel.h"

#include <QtGui/QIcon>
#include <QtCore/QStringList>

namespace TextEditor {

class TEXTEDITOR_EXPORT Keywords
{
public:
    Keywords();
    Keywords(const QStringList &variabels, const QStringList &functions, const QMap<QString, QStringList> &functionArgs);
    bool isVariable(const QString &word) const;
    bool isFunction(const QString &word) const;

    QStringList variables() const;
    QStringList functions() const;
    QStringList argsForFunction(const QString &function) const;

private:
    QStringList m_variables;
    QStringList m_functions;
    QMap<QString, QStringList> m_functionArgs;
};

class TEXTEDITOR_EXPORT KeywordsAssistProposalItem : public TextEditor::BasicProposalItem
{
public:
    KeywordsAssistProposalItem(Keywords keywords);
    virtual ~KeywordsAssistProposalItem();

    virtual bool prematurelyApplies(const QChar &c) const;
    virtual void applyContextualContent(TextEditor::BaseTextEditor *editor,
                                        int basePosition) const;
private:
    Keywords m_keywords;
};

class TEXTEDITOR_EXPORT KeywordsFunctionHintModel : public TextEditor::IFunctionHintProposalModel
{
public:
    KeywordsFunctionHintModel(const QStringList &functionSymbols);
    virtual ~KeywordsFunctionHintModel();

    virtual void reset();
    virtual int size() const;
    virtual QString text(int index) const;
    virtual int activeArgument(const QString &prefix) const;

private:
    QStringList m_functionSymbols;
};

class TEXTEDITOR_EXPORT KeywordsCompletionAssistProcessor : public TextEditor::IAssistProcessor
{
public:
    KeywordsCompletionAssistProcessor(Keywords keywords);
    virtual ~KeywordsCompletionAssistProcessor();

    virtual TextEditor::IAssistProposal *perform(const TextEditor::IAssistInterface *interface);
    virtual QChar startOfCommentChar() const;

private:
    bool acceptsIdleEditor();
    int findStartOfName(int pos = -1);
    bool isInComment() const;
    void addWordsToProposalList(QList<BasicProposalItem *> *items, const QStringList &words, const QIcon &icon);

    int m_startPosition;
    QString m_word;
    QScopedPointer<const TextEditor::IAssistInterface> m_interface;
    const QIcon m_variableIcon;
    const QIcon m_functionIcon;
    Keywords m_keywords;
};

} // TextEditor

#endif // KEYWORDSCOMPLETIONASSISTPROCESSOR_H
