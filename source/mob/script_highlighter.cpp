#include "script_highlighter.h"
#include "ui_theme.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QStringList>

CScriptHighlighter::CScriptHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent)
{
    HighlightingRule rule;
    QTextCharFormat format;

    format.setForeground(ei::uiTextColor(ei::UiTextTone::Link));
    format.setFontWeight(QFont::Bold);
    const QStringList keywords = {
        "\\bGlobalVars\\b", "\\bDeclareScript\\b", "\\bScript\\b",
        "\\bWorldScript\\b", "\\bif\\b", "\\bthen\\b", "\\bNULL\\b"
    };
    for(const QString& pattern : keywords)
    {
        rule.pattern = QRegExp(pattern);
        rule.format = format;
        m_rules.append(rule);
    }

    format = QTextCharFormat();
    format.setForeground(ei::uiTextColor(ei::UiTextTone::Warning));
    rule.pattern = QRegExp("\".*\"");
    rule.format = format;
    m_rules.append(rule);

    format = QTextCharFormat();
    format.setForeground(ei::uiTextColor(ei::UiTextTone::Incomplete));
    format.setFontWeight(QFont::Bold);
    rule.pattern = QRegExp("\\b(ActivateTrap|Add|AddLoot|AddMob|AddObject|AddRectToArea|AddRoundToArea|AddUnitToParty|AddUnitToServer|AddUnitUnderControl|AlarmPosX|AlarmPosY|AlarmTime|Any|AttachParticles|AttachParticleSource|Attack|BlockUnit|Cast|CastSpellPoint|CastSpellUnit|ConsoleFloat|ConsoleString|CopyItems|CopyLoot|CopyStats|Crawl|CreateFX|CreateFXSource|CreateLightning|CreateParticleSource|CreateParty|CreatePointLight|CreateRandomizeFXSource|DeleteArea|DeleteFXSource|DeleteLightning|DeleteParticleSource|DeletePointLight|DistanceUnitPoint|DistanceUnitUnit|Div|EnableLever|EraseQuestItem|Every|FixItems|FixWorldTime|Follow|For|ForIf|GetAIClass|GetBSZValue|GetDiplomacy|GetFutureX|GetFutureY|GetLeader|GetLeverState|GetLootItemsCount|GetMercsNumber|GetMoney|GetObject|GetObjectByID|GetObjectByName|GetObjectID|GetPlayer|GetPlayerUnits|GetUnitOfPlayer|GetWorldTime|GetX|GetY|GetZ|GetZValue|GiveDexterity|GiveIntelligence|GiveItem|GiveMoney|GiveQuestItem|GiveSkill|GiveStrength|GiveUnitQuestItem|GiveUnitSpell|GodMode|GroupAdd|GroupCross|GroupHas|GroupSee|GroupSize|GroupSub|GSDelVar|GSGetVar|GSSetVar|GSSetVarMax|Guard|HaveItem|HideObject|HP|Idle|InflictDamage|InvokeAlarm|IsAlarm|IsAlive|IsCameraPlaying|IsDead|IsEnemy|IsEqual|IsEqualString|IsGreater|IsInArea|IsInSquare|IsLess|IsNight|IsPlayerInDanger|IsPlayerInSafety|IsUnitBlocked|IsUnitInWater|IsUnitVisible|KillScript|KillUnit|LeaveToZone|Lie|Mana|MaxHP|MaxMana|MoveParticleSource|MovePointLight|MoveToObject|MoveToPoint|Mul|Not|PlayAnimation|PlayCamera|PlayerSee|PlayFX|PlayMovie|QFinish|QObjArea|QObjGetItem|QObjKillGroup|QObjKillUnit|QObjSeeObject|QObjSeeUnit|QObjUse|QStart|QuestComplete|Random|RecalcMercBriefings|RedeployParty|RemoveObject|RemoveObjectFromServer|RemoveParty|RemoveQuestItem|RemoveUnitFromControl|RemoveUnitFromParty|RemoveUnitFromServer|ResetTarget|Rest|RotateTo|Run|RunWorldTime|SendEvent|SendStringEvent|Sentry|SetCameraOrientation|SetCameraPosition|SetCP|SetCPFast|SetCurrentParty|SetDiplomacy|SetDirectionToObject|SetEnemy|SetParticleSourceSize|SetPlayer|SetPlayerAggression|SetScience|SetSpellAggression|SetSunLight|SetWaterLevel|SetWind|ShowBitmap|ShowCredits|Sleep|SleepUntil|SleepUntilIdle|Stand|StartAnimation|Sub|SwitchLeverState|SwitchLeverStateEx|UMAg|UMAggression|UMClear|UMCorpseWatcher|UMFear|UMFollow|UMGuard|UMGuardEx|UMPatrol|UMPatrolAddPoint|UMPatrolAddPointLook|UMPatrolClear|UMPlayer|UMRevenge|UMSentry|Walk|CreateRandomizedFXSource|PlayMusic|SetBackGroundColor|UMStandard|UMSuspection|UnitInSquare|UnitSee|WaitEndAnimation|WaitSegment|WasLooted|GetPlayerOfUnit|GetUnitExp|GetArmor|GetObjectStringID|FloatToStr|StrToFloat|StrCat|StrLen|Sin|Cos|Pow|DistancePointPoint|NearestUnit|UnitsNearPoint)(?!.*\")\\b");
    rule.format = format;
    m_rules.append(rule);

    m_singleLineCommentFormat.setForeground(Qt::lightGray);
    rule.pattern = QRegExp("//[^\n]*");
    rule.format = m_singleLineCommentFormat;
    m_rules.append(rule);

    m_multiLineCommentFormat.setForeground(Qt::lightGray);
    m_commentStart = QRegExp("/\\*");
    m_commentEnd = QRegExp("\\*/");

    format = QTextCharFormat();
    format.setFontWeight(QFont::Bold);
    format.setForeground(QBrush(QColor(80, 232, 209)));
    rule.pattern = QRegExp("(object|float|group|string)((?=,|\\s+|\\))|$)");
    rule.format = format;
    m_rules.append(rule);

    format = QTextCharFormat();
    format.setFontWeight(QFont::Bold);
    format.setForeground(QBrush(QColor(224, 217, 26)));
    rule.pattern = QRegExp("\\-?\\b[\\d.]+\\b(?![a-zA-Z ]*\")");
    rule.format = format;
    m_rules.append(rule);
}

void CScriptHighlighter::highlightBlock(const QString& text)
{
    for(const HighlightingRule& rule : m_rules)
    {
        QRegExp expression(rule.pattern);
        int index = expression.indexIn(text);
        while(index >= 0)
        {
            const int length = expression.matchedLength();
            setFormat(index, length, rule.format);
            index = expression.indexIn(text, index + length);
        }
    }

    setCurrentBlockState(0);
    int startIndex = previousBlockState() == 1 ? 0 : m_commentStart.indexIn(text);
    while(startIndex >= 0)
    {
        const int endIndex = m_commentEnd.indexIn(text, startIndex);
        int commentLength = 0;
        if(endIndex == -1)
        {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        }
        else
            commentLength = endIndex - startIndex + m_commentEnd.matchedLength();
        setFormat(startIndex, commentLength, m_multiLineCommentFormat);
        startIndex = m_commentStart.indexIn(text, startIndex + commentLength);
    }
}
