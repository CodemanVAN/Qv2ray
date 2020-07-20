#include "w_RoutesEditor.hpp"

#include "components/plugins/QvPluginHost.hpp"
#include "core/connection/Generation.hpp"
#include "core/handler/ConfigHandler.hpp"
#include "ui/common/UIBase.hpp"
#include "ui/node/models/InboundNodeModel.hpp"
#include "ui/node/models/OutboundNodeModel.hpp"
#include "ui/node/models/RuleNodeModel.hpp"
#include "ui/windows/w_ImportConfig.hpp"
#include "w_InboundEditor.hpp"
#include "w_JsonEditor.hpp"
#include "w_OutboundEditor.hpp"

#include <nodes/internal/FlowScene.hpp>
#include <nodes/internal/FlowView.hpp>
#include <nodes/internal/FlowViewStyle.hpp>
#include <nodes/internal/NodeStyle.hpp>

using QtNodes::FlowView;
using namespace Qv2ray::ui::nodemodels;

//#define CurrentRule this->rules[this->currentRuleTag]
#define LOADINGCHECK                                                                                                                            \
    if (isLoading)                                                                                                                              \
        return;
#define GetFirstNodeData(_node, name)                                                                                                           \
    (static_cast<name##Data *>(static_cast<Qv##name##Model *>((nodeScene->node(_node))->nodeDataModel())->outData(0).get()))

#define CHECKEMPTYRULES
//    if (this->rules.isEmpty())                                                                                                                  \
//    {                                                                                                                                           \
//        LOG(MODULE_UI, "No rules currently, we add one.")                                                                                       \
//        nodeDispatcher->CreateRule({});                                                                                                         \
//    }

#define LOAD_FLAG_BEGIN isLoading = true;
#define LOAD_FLAG_END isLoading = false;

void RouteEditor::SetupNodeWidget()
{
    if (GlobalConfig.uiConfig.useDarkTheme)
    {
        ConnectionStyle::setConnectionStyle(
            R"({"ConnectionStyle": {"ConstructionColor": "gray","NormalColor": "black","SelectedColor": "gray",
                                            "SelectedHaloColor": "deepskyblue","HoveredColor": "deepskyblue","LineWidth": 3.0,
                                            "ConstructionLineWidth": 2.0,"PointDiameter": 10.0,"UseDataDefinedColors": true}})");
    }
    else
    {
        QtNodes::NodeStyle::setNodeStyle(
            R"({"NodeStyle": {"NormalBoundaryColor": "darkgray","SelectedBoundaryColor": "deepskyblue",
                                            "GradientColor0": "mintcream","GradientColor1": "mintcream","GradientColor2": "mintcream",
                                            "GradientColor3": "mintcream","ShadowColor": [200, 200, 200],"FontColor": [10, 10, 10],
                                            "FontColorFaded": [100, 100, 100],"ConnectionPointColor": "white","PenWidth": 2.0,"HoveredPenWidth": 2.5,
                                            "ConnectionPointDiameter": 10.0,"Opacity": 1.0}})");
        QtNodes::FlowViewStyle::setStyle(
            R"({"FlowViewStyle": {"BackgroundColor": [255, 255, 240],"FineGridColor": [245, 245, 230],"CoarseGridColor": [235, 235, 220]}})");
        ConnectionStyle::setConnectionStyle(
            R"({"ConnectionStyle": {"ConstructionColor": "gray","NormalColor": "black","SelectedColor": "gray",
                                            "SelectedHaloColor": "deepskyblue","HoveredColor": "deepskyblue","LineWidth": 3.0,"ConstructionLineWidth": 2.0,
                                            "PointDiameter": 10.0,"UseDataDefinedColors": false}})");
    }

    QVBoxLayout *l = new QVBoxLayout(nodeGraphWidget);
    nodeScene = new FlowScene(nodeGraphWidget);
    connect(nodeScene, &FlowScene::nodeClicked, this, &RouteEditor::onNodeClicked);
    connect(nodeScene, &FlowScene::connectionCreated, this, &RouteEditor::onConnectionCreated);
    connect(nodeScene, &FlowScene::connectionDeleted, this, &RouteEditor::onConnectionDeleted);
    auto view = new FlowView(nodeScene);
    // view->scaleDown();
    l->addWidget(view);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
}

RouteEditor::RouteEditor(QJsonObject connection, QWidget *parent) : QvDialog(parent), root(connection), original(connection)
{
    setupUi(this);
    QvMessageBusConnect(RouteEditor);
    //
    nodeDispatcher = std::make_shared<NodeDispatcher>(this);
    connect(nodeDispatcher.get(), &NodeDispatcher::OnInboundCreated, this, &RouteEditor::OnDispatcherInboundCreated);
    connect(nodeDispatcher.get(), &NodeDispatcher::OnOutboundCreated, this, &RouteEditor::OnDispatcherOutboundCreated);
    connect(nodeDispatcher.get(), &NodeDispatcher::OnRuleCreated, this, &RouteEditor::OnDispatcherRuleCreated);
    //
    isLoading = true;
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
    SetupNodeWidget();
    updateColorScheme();
    //
    domainStrategy = root["routing"].toObject()["domainStrategy"].toString();
    domainStrategyCombo->setCurrentText(domainStrategy);
    //
    //// Show connections in the node graph
    for (const auto &in : root["inbounds"].toArray())
    {
        auto _ = nodeDispatcher->CreateInbound(INBOUND(in.toObject()));
    }

    for (const auto &out : root["outbounds"].toArray())
    {
        OutboundObjectMeta meta;
        meta.loadJson(out.toObject()[META_OUTBOUND_KEY_NAME].toObject());
        meta.realOutbound = OUTBOUND(out.toObject());
        auto _ = nodeDispatcher->CreateOutbound(meta);
    }

    for (const auto &item : root["routing"].toObject()["rules"].toArray())
    {
        auto _ = nodeDispatcher->CreateRule(RuleObject::fromJson(item.toObject()));
    }

    //// Set default outboung combo text AFTER adding all outbounds.
    defaultOutbound = getTag(OUTBOUND(root["outbounds"].toArray().first().toObject()));
    defaultOutboundCombo->setCurrentText(defaultOutbound);
    //
    ////    // Find and add balancers.
    ////    for (auto _balancer : root["routing"].toObject()["balancers"].toArray())
    ////    {
    ////        auto _balancerObject = _balancer.toObject();
    ////        if (!_balancerObject["tag"].toString().isEmpty())
    ////        {
    ////            balancers.insert(_balancerObject["tag"].toString(), _balancerObject["selector"].toVariant().toStringList());
    ////        }
    ////    }
    //
    for (const auto &group : ConnectionManager->AllGroups())
    {
        importGroupBtn->addItem(GetDisplayName(group), group.toString());
    }
    //
    // isLoading = false;
}

QvMessageBusSlotImpl(RouteEditor)
{
    switch (msg)
    {
        MBShowDefaultImpl;
        MBHideDefaultImpl;
        MBRetranslateDefaultImpl;
        MBUpdateColorSchemeDefaultImpl;
    }
}

void RouteEditor::onNodeClicked(Node &n)
{
    // LOADINGCHECK
    //
    // if (isExiting)
    //     return;
    //
    // auto isOut = outboundNodes.values().contains(n.id());
    // auto isIn = inboundNodes.values().contains(n.id());
    // auto isRule = ruleNodes.values().contains(n.id());
    //
    // /* if (isRule)
    //  {
    //      // It's a rule object
    //      currentRuleTag = GetFirstNodeData(n.id(), RuleNode)->GetRuleTag();
    //      DEBUG(MODULE_GRAPH, "Selecting rule: " + currentRuleTag)
    //      // ShowCurrentRuleDetail();
    //      toolBox->setCurrentIndex(1);
    //  }
    //  else*/
    // if (isOut || isIn)
    // {
    //     // It's an inbound or an outbound.
    //     QString tag;
    //     QString host;
    //     int port;
    //     QString protocol;
    //
    //     if (isOut)
    //     {
    //         const auto root = GetFirstNodeData(n.id(), OutboundNode)->GetOutbound();
    //         GetOutboundInfo(*root, &host, &port, &protocol);
    //     }
    //     else
    //     {
    //         const auto root = GetFirstNodeData(n.id(), InboundNode)->GetInbound();
    //         GetInboundInfo(*root, &host, &port, &protocol);
    //     }
    //
    //     tagLabel->setText(tag);
    //     protocolLabel->setText(protocol);
    //     portLabel->setText(QSTRN(port));
    //     hostLabel->setText(host);
    // }
    // else
    // {
    //     LOG(MODULE_GRAPH, "Selected an unknown node, RARE.")
    // }
}

void RouteEditor::OnDispatcherInboundCreated(std::shared_ptr<INBOUND> in)
{
    auto _nodeData = std::make_unique<InboundNodeModel>(nodeDispatcher, in);
    auto &node = nodeScene->createNode(std::move(_nodeData));
    //
    QPointF pos{ 0 + GRAPH_GLOBAL_OFFSET_X, nodeDispatcher->InboundsCount() * 130.0 + GRAPH_GLOBAL_OFFSET_Y };
    nodeScene->setNodePosition(node, pos);
}

void RouteEditor::OnDispatcherOutboundCreated(std::shared_ptr<OutboundObjectMeta> out)
{
    auto _nodeData = std::make_unique<OutboundNodeModel>(nodeDispatcher, out);
    auto &node = nodeScene->createNode(std::move(_nodeData));
    //
    auto pos = nodeGraphWidget->pos();
    pos.setX(pos.x() + 850 + GRAPH_GLOBAL_OFFSET_X);
    pos.setY(pos.y() + nodeDispatcher->OutboundsCount() * 120 + GRAPH_GLOBAL_OFFSET_Y);
    //
    nodeScene->setNodePosition(node, pos);
    defaultOutboundCombo->addItem(out->getTag());
}

void RouteEditor::OnDispatcherRuleCreated(std::shared_ptr<RuleObject> rule)
{
    auto _nodeData = std::make_unique<RuleNodeModel>(nodeDispatcher, rule);
    auto &node = nodeScene->createNode(std::move(_nodeData));
    //
    auto pos = nodeGraphWidget->pos();
    pos.setX(pos.x() + 350 + GRAPH_GLOBAL_OFFSET_X);
    pos.setY(pos.y() + nodeDispatcher->RulesCount() * 120 + GRAPH_GLOBAL_OFFSET_Y);
    //
    nodeScene->setNodePosition(node, pos);
    ruleListWidget->addItem(rule->QV2RAY_RULE_TAG);
}

void RouteEditor::onConnectionCreated(QtNodes::Connection const &c)
{
    LOADINGCHECK

    if (isExiting)
        return;

    // Connection Established
    auto const &sourceNode = c.getNode(PortType::Out);
    auto const &targetNode = c.getNode(PortType::In);

    // if (inboundNodes.values().contains(sourceNode->id()) && ruleNodes.values().contains(targetNode->id()))
    // {
    //     // It's a inbound-rule connection
    //     onNodeClicked(*sourceNode);
    //     onNodeClicked(*targetNode);
    //     LOG(MODULE_GRAPH, "Inbound-rule new connection.")
    //     // Get all connected inbounds to this rule node.
    //     // QStringList has an helper to let us remove duplicates, see below.
    //     QSet<QString> _inbounds;
    //     //
    //     // Workaround for removing a connection within the loop.
    //     QList<std::shared_ptr<QtNodes::Connection>> connectionsTobeRemoved;
    //     for (auto &&[_, conn] : nodeScene->connections())
    //     {
    //         const auto &inNode = conn->getNode(PortType::In);
    //         const auto &outNode = conn->getNode(PortType::Out);
    //         // If a connection is not current Id, but with same IN/OUT nodes.
    //         // It is a "duplicated" connection.
    //         if (inNode->id() == targetNode->id() && outNode->id() == sourceNode->id() && conn->id() != c.id())
    //         {
    //             connectionsTobeRemoved << (conn);
    //         }
    //         // Append all inbounds
    //         if (inNode->id() == targetNode->id())
    //         {
    //             _inbounds.insert(GetFirstNodeData(outNode->id(), InboundNode)->GetInbound());
    //         }
    //     }
    //     for (const auto &connRemoved : connectionsTobeRemoved)
    //     {
    //         nodeScene->deleteConnection(*connRemoved);
    //     }
    //
    //     CurrentRule.inboundTag = _inbounds.values();
    // }
    // else if (ruleNodes.values().contains(sourceNode->id()) && outboundNodes.values().contains(targetNode->id()))
    // {
    //     // It's a rule-outbound connection
    //     onNodeClicked(*sourceNode);
    //     onNodeClicked(*targetNode);
    //     CurrentRule.outboundTag = GetFirstNodeData(targetNode->id(), OutboundNode)->GetOutbound();
    //     // Connecting to an outbound will disable the balancer feature.
    //     CurrentRule.QV2RAY_RULE_USE_BALANCER = false;
    //     // Update balancer settings.
    //     // ShowCurrentRuleDetail();
    //     LOG(MODULE_GRAPH, "Updated outbound: " + CurrentRule.outboundTag)
    // }
    // else
    // {
    //     // It's an impossible connection
    //     LOG(MODULE_GRAPH, "Unrecognized connection, RARE.")
    // }
}

void RouteEditor::onConnectionDeleted(QtNodes::Connection const &c)
{
    LOADINGCHECK

    if (isExiting)
        return;

    // Connection Deleted
    const auto &source = c.getNode(PortType::Out);
    const auto &target = c.getNode(PortType::In);

    // if (inboundNodes.values().contains(source->id()) && ruleNodes.values().contains(target->id()))
    //{
    //    // It's a inbound-rule connection
    //    onNodeClicked(*source);
    //    onNodeClicked(*target);
    //    currentRuleTag = GetFirstNodeData(target->id(), RuleNode)->GetRuleTag();
    //    auto _inboundTag = GetFirstNodeData(source->id(), InboundNode)->GetInbound();
    //    LOG(MODULE_UI, "Removing inbound: " + _inboundTag + " from rule: " + currentRuleTag)
    //    CurrentRule.inboundTag.removeAll(_inboundTag);
    //}
    // else if (ruleNodes.values().contains(source->id()) && outboundNodes.values().contains(target->id()))
    //{
    //    // It's a rule-outbound connection
    //    onNodeClicked(*source);
    //    onNodeClicked(*target);
    //    currentRuleTag = GetFirstNodeData(source->id(), RuleNode)->GetRuleTag();
    //    auto _outboundTag = GetFirstNodeData(target->id(), OutboundNode)->GetOutbound();
    //
    //    if (!CurrentRule.QV2RAY_RULE_USE_BALANCER && CurrentRule.outboundTag == _outboundTag)
    //    {
    //        CurrentRule.outboundTag.clear();
    //    }
    //
    //    LOG(MODULE_GRAPH, "Removing an outbound: " + _outboundTag)
    //}
    // else
    //{
    //    // It's an impossible connection
    //    LOG(MODULE_GRAPH, "Selected an unknown node, RARE.")
    //}
}

CONFIGROOT RouteEditor::OpenEditor()
{
    auto result = this->exec();

    //    if (rules.isEmpty())
    //    {
    //        // Prevent empty rule list causing mis-detection of config type to
    //        // simple.
    //        on_addRouteBtn_clicked();
    //    }

    // // If clicking OK
    // if (result == QDialog::Accepted)
    // {
    // QJsonArray rulesArray;
    // QJsonArray _balancers;
    //
    //// Append rules by order
    // for (auto i = 0; i < ruleListWidget->count(); i++)
    //{
    //    auto _rule = rules[i];
    //    auto ruleJsonObject = _rule.toJson();
    //
    //    // Process balancer for a rule
    //    if (_rule.QV2RAY_RULE_USE_BALANCER)
    //    {
    //        // Do not use outbound tag.
    //        ruleJsonObject.remove("outboundTag");
    //
    //        //                // Find balancer list
    //        //                if (!balancers.contains(_rule.balancerTag))
    //        //                {
    //        //                    LOG(MODULE_UI, "Cannot find a balancer for tag: " + _rule.balancerTag)
    //        //                }
    //        //                else
    //        //                {
    //        //                    auto _balancerList = balancers[_rule.balancerTag];
    //        //                    QJsonObject balancerEntry;
    //        //                    balancerEntry["tag"] = _rule.balancerTag;
    //        //                    balancerEntry["selector"] = QJsonArray::fromStringList(_balancerList);
    //        //                    _balancers.append(balancerEntry);
    //        //                }
    //    }
    //
    //    // Remove some empty fields.
    //    if (_rule.port.isEmpty())
    //    {
    //        ruleJsonObject.remove("port");
    //    }
    //
    //    if (_rule.network.isEmpty())
    //    {
    //        ruleJsonObject.remove("network");
    //    }
    //
    //  rulesArray.append(ruleJsonObject);
    //}

    // QJsonObject routing;
    // routing["domainStrategy"] = domainStrategy;
    // routing["rules"] = rulesArray;
    // routing["balancers"] = _balancers;
    // //
    // QJsonArray _inbounds;
    // QJsonArray _outbounds;
    //
    // // Convert our internal data format to QJsonArray
    // for (auto x : inbounds)
    // {
    //     if (x.isEmpty())
    //         continue;
    //
    //     _inbounds.append(x.raw());
    // }
    //
    // for (auto x : outbounds)
    // {
    //     if (x.isEmpty())
    //         continue;
    //
    //     if (getTag(x) == defaultOutbound)
    //     {
    //         LOG(MODULE_CONNECTION, "Pushing default outbound to the front.")
    //         // Put the default outbound to the first.
    //         _outbounds.push_front(x.raw());
    //     }
    //     else
    //     {
    //         _outbounds.push_back(x.raw());
    //     }
    // }
    //
    // root["inbounds"] = _inbounds;
    // root["outbounds"] = _outbounds;
    // root["routing"] = routing;
    //    return root;
    //}
    // else
    //{
    return original;
    //}
}

RouteEditor::~RouteEditor()
{
    // Double prevent events to be processed while closing the Editor.
    isLoading = true;
    disconnect(nodeScene, &FlowScene::connectionDeleted, this, &RouteEditor::onConnectionDeleted);
    disconnect(nodeScene, &FlowScene::connectionCreated, this, &RouteEditor::onConnectionCreated);
    disconnect(nodeScene, &FlowScene::nodeClicked, this, &RouteEditor::onNodeClicked);
}
void RouteEditor::on_buttonBox_accepted()
{
}

void RouteEditor::on_insertDirectBtn_clicked()
{
    // auto freedom = GenerateFreedomOUT("AsIs", "", 0);
    // auto tag = "Freedom_" + QSTRN(QTime::currentTime().msecsSinceStartOfDay());
    // auto out = GenerateOutboundEntry("freedom", freedom, QJsonObject(), QJsonObject(), "0.0.0.0", tag);
    //// ADD NODE
    // AddOutbound(out);
    // statusLabel->setText(tr("Added DIRECT outbound"));
}

void RouteEditor::on_addDefaultBtn_clicked()
{
    LOADINGCHECK
    // Add default connection from GlobalConfig
    //
    const auto &inboundConfig = GlobalConfig.inboundConfig;
    QJsonObject sniffingOff{ { "enabled", false } };
    QJsonObject sniffingOn{ { "enabled", true }, { "destOverride", QJsonArray{ "http", "tls" } } };
    //
    if (inboundConfig.useHTTP)
    {
        auto http = GenerateHTTPIN(QList<AccountObject>() << inboundConfig.httpSettings.account);
        INBOUND httpConfig = GenerateInboundEntry(inboundConfig.listenip, inboundConfig.httpSettings.port, "http", http, "http_gConf",
                                                  inboundConfig.httpSettings.sniffing ? sniffingOn : sniffingOff);
        auto _ = nodeDispatcher->CreateInbound(httpConfig);
    }
    if (inboundConfig.useSocks)
    {
        auto socks = GenerateSocksIN((inboundConfig.socksSettings.useAuth ? "password" : "noauth"), //
                                     QList<AccountObject>() << inboundConfig.socksSettings.account, //
                                     inboundConfig.socksSettings.enableUDP,                         //
                                     inboundConfig.socksSettings.localIP);
        auto socksConfig = GenerateInboundEntry(inboundConfig.listenip,           //
                                                inboundConfig.socksSettings.port, //
                                                "socks",                          //
                                                socks,                            //
                                                "SOCKS_gConf",                    //
                                                (inboundConfig.socksSettings.sniffing ? sniffingOn : sniffingOff));
        auto _ = nodeDispatcher->CreateInbound(socksConfig);
    }

    if (inboundConfig.useTPROXY)
    {
        QList<QString> networks;
#define ts inboundConfig.tProxySettings
        if (ts.hasTCP)
            networks << "tcp";
        if (ts.hasUDP)
            networks << "udp";
        const auto tproxy_network = networks.join(",");
        auto tproxyInSettings = GenerateDokodemoIN("", 0, tproxy_network, 0, true, 0);
        //
        //
        const static QJsonObject tproxy_sniff{ { "enabled", true }, { "destOverride", QJsonArray{ "http", "tls" } } };
        const QJsonObject tproxy_streamSettings{ { "sockopt", QJsonObject{ { "tproxy", ts.mode } } } };

        auto tProxyIn = GenerateInboundEntry(ts.tProxyIP, ts.port, "dokodemo-door", tproxyInSettings, "TPROXY_gConf");
        tProxyIn.insert("sniffing", tproxy_sniff);
        tProxyIn.insert("streamSettings", tproxy_streamSettings);
        auto _ = nodeDispatcher->CreateInbound(tProxyIn);

        if (!ts.tProxyV6IP.isEmpty())
        {
            auto tProxyV6In = GenerateInboundEntry(ts.tProxyV6IP, ts.port, "dokodemo-door", tproxyInSettings, "TPROXY_gConf_V6");
            tProxyV6In.insert("sniffing", tproxy_sniff);
            tProxyV6In.insert("streamSettings", tproxy_streamSettings);
            auto _ = nodeDispatcher->CreateInbound(tProxyV6In);
        }
#undef ts
    }

    CHECKEMPTYRULES
}
void RouteEditor::on_insertBlackBtn_clicked()
{
    LOADINGCHECK
    auto blackHole = GenerateBlackHoleOUT(false);
    auto tag = "blackhole_" + QSTRN(QTime::currentTime().msecsSinceStartOfDay());
    auto _blackHoleOutbound = GenerateOutboundEntry("blackhole", blackHole, {}, {}, "0.0.0.0", tag);
    // AddOutbound(_blackHoleOutbound);
}
void RouteEditor::on_addInboundBtn_clicked()
{
    LOADINGCHECK
    InboundEditor w(INBOUND(), this);
    auto _result = w.OpenEditor();

    if (w.result() == QDialog::Accepted)
    {
        auto _ = nodeDispatcher->CreateInbound(_result);
    }

    CHECKEMPTYRULES
}
void RouteEditor::on_addOutboundBtn_clicked()
{
    LOADINGCHECK
    ImportConfigWindow w(this);
    // True here for not keep the inbounds.
    auto configs = w.SelectConnection(true);

    for (auto i = 0; i < configs.count(); i++)
    {
        auto conf = configs.values()[i];
        auto name = configs.key(conf, "");

        if (name.isEmpty())
            continue;

        // conf is rootObject, needs to unwrap it.
        auto confList = conf["outbounds"].toArray();

        for (int i = 0; i < confList.count(); i++)
        {
            auto _ = nodeDispatcher->CreateOutbound(make_outbound(OUTBOUND(confList[i].toObject())));
        }
    }

    CHECKEMPTYRULES
}

void RouteEditor::on_delBtn_clicked()
{
    if (nodeScene->selectedNodes().empty())
    {
        QvMessageBoxWarn(this, tr("Remove Items"), tr("Please select a node from the graph to continue."));
        return;
    }

    auto firstNode = nodeScene->selectedNodes()[0];
    auto isInbound = false;  // inboundNodes.values().contains(firstNode->id());
    auto isOutbound = false; // outboundNodes.values().contains(firstNode->id());
    auto isRule = false;     // ruleNodes.values().contains(firstNode->id());

    // Get the tag first, and call inbounds/outbounds/rules container variable
    // remove() Remove the node last since some events may trigger. Then remove
    // the node container.
    if (isInbound)
    {
        // currentInboundOutboundTag = GetFirstNodeData(firstNode->id(), InboundNode)->GetInbound();
        // nodeScene->removeNode(*nodeScene->node(inboundNodes[currentInboundOutboundTag]));
        // inboundNodes.remove(currentInboundOutboundTag);
        //
        // // Remove corresponded inbound tags from the rules.
        // for (auto k : rules.keys())
        // {
        //     auto v = rules[k];
        //     v.inboundTag.removeAll(currentInboundOutboundTag);
        //     rules[k] = v;
        // }
        //
        // inbounds.remove(currentInboundOutboundTag);
    }
    else if (isOutbound)
    {
        // currentInboundOutboundTag = GetFirstNodeData(firstNode->id(), OutboundNode)->GetOutbound();
        // outbounds.remove(currentInboundOutboundTag);
        // ResolveDefaultOutboundTag(currentInboundOutboundTag, "");
        //
        //// Remove corresponded outbound tags from the rules.
        // for (auto k : rules.keys())
        //{
        //    auto v = rules[k];
        //
        //    if (v.outboundTag == currentInboundOutboundTag)
        //        v.outboundTag.clear();
        //
        //    rules[k] = v;
        //}
        //
        // nodeScene->removeNode(*nodeScene->node(outboundNodes[currentInboundOutboundTag]));
        // outboundNodes.remove(currentInboundOutboundTag);
    }
    else if (isRule)
    {
        // ruleEnableCB->setEnabled(false);
        // ruleTagLineEdit->setEnabled(false);
        // ruleRenameBtn->setEnabled(false);
        // auto RuleTag = GetFirstNodeData(firstNode->id(), RuleNode)->GetRuleTag();
        // currentRuleTag.clear();
        // routeRuleGroupBox->setEnabled(false);
        // routeEditGroupBox->setEnabled(false);
        // rules.remove(RuleTag);
        // nodeScene->removeNode(*nodeScene->node(ruleNodes[RuleTag]));
        // ruleNodes.remove(RuleTag);
        ////
        //// Remove item from the rule order list widget.
        // ruleListWidget->takeItem(ruleListWidget->row(ruleListWidget->findItems(RuleTag, Qt::MatchExactly).first()));
        // CHECKEMPTYRULES
        //// currentRuleTag = rules.firstKey();
        // ShowCurrentRuleDetail();
    }
    else
    {
        LOG(MODULE_UI, "Unknown node selected.")
        QvMessageBoxWarn(this, tr("Error"), tr("Qv2ray entered an unknown state."));
    }
}
void RouteEditor::on_addRouteBtn_clicked()
{
    auto ruleName = nodeDispatcher->CreateRule({});
    Q_UNUSED(ruleName)
}
void RouteEditor::on_editBtn_clicked()
{
    if (nodeScene->selectedNodes().empty())
    {
        QvMessageBoxWarn(this, tr("Edit Inbound/Outbound"), tr("Please select a node from the graph to continue."));
        return;
    }

    const auto firstNode = nodeScene->selectedNodes().at(0);
    const auto isInbound = false;  // inboundNodes.values().contains(firstNode->id());
    const auto isOutbound = false; // outboundNodes.values().contains(firstNode->id());

    if (isInbound)
    {
        // currentInboundOutboundTag = GetFirstNodeData(firstNode->id(), InboundNode)->GetInbound();
        //
        // if (!inbounds.contains(currentInboundOutboundTag))
        // {
        //     QvMessageBoxWarn(this, tr("Edit Inbound"), tr("No inbound tag found: ") + currentInboundOutboundTag);
        //     return;
        // }
        //
        // auto _in = inbounds[currentInboundOutboundTag];
        // INBOUND _result;
        // auto protocol = _in["protocol"].toString();
        // int _code;
        //
        // if (protocol != "http" && protocol != "mtproto" && protocol != "socks" && protocol != "dokodemo-door")
        // {
        //     QvMessageBoxWarn(this, tr("Cannot Edit"),
        //                      tr("Currently, this type of outbound is not supported by the editor.") + "\r\n" +
        //                          tr("We will launch Json Editor instead."));
        //     statusLabel->setText(tr("Opening JSON editor"));
        //     JsonEditor w(_in, this);
        //     _result = INBOUND(w.OpenEditor());
        //     _code = w.result();
        // }
        // else
        // {
        //     InboundEditor w(_in, this);
        //     statusLabel->setText(tr("Opening default inbound editor"));
        //     _result = w.OpenEditor();
        //     _code = w.result();
        // }
        //
        // statusLabel->setText(tr("OK"));
        //
        // if (_code == QDialog::Accepted)
        // {
        //     bool isTagChanged = getTag(_in) != getTag(_result);
        //
        //     if (isTagChanged)
        //     {
        //         auto newTag = getTag(_result);
        //         RenameItemTag(RENAME_INBOUND, getTag(_in), &newTag);
        //     }
        //
        //     DEBUG(MODULE_UI, "Removed old tag: " + getTag(_in))
        //     inbounds.remove(getTag(_in));
        //     DEBUG(MODULE_UI, "Adding new tag: " + getTag(_result))
        //     inbounds.insert(getTag(_result), _result);
        // }
    }
    else if (isOutbound)
    {
        // currentInboundOutboundTag = GetFirstNodeData(firstNode->id(), OutboundNode)->GetOutbound();
        //
        // if (!outbounds.contains(currentInboundOutboundTag))
        //{
        //    QvMessageBoxWarn(this, tr("Edit Inbound"), tr("No inbound tag found: ") + currentInboundOutboundTag);
        //    return;
        //}
        //
        // OUTBOUND _result;
        // auto _out = outbounds.value(currentInboundOutboundTag);
        // auto protocol = _out["protocol"].toString().toLower();
        // int _code;
        //
        // bool guisupport = true;
        // if (protocol != "vmess" && protocol != "shadowsocks" && protocol != "socks" && protocol != "http")
        //{
        //    guisupport = false;
        //    auto pluginEditorWidgetsInfo = PluginHost->GetOutboundEditorWidgets();
        //    for (const auto &plugin : pluginEditorWidgetsInfo)
        //    {
        //        for (const auto &_d : plugin->OutboundCapabilities())
        //        {
        //            guisupport = guisupport || protocol == _d.protocol;
        //        }
        //    }
        //}
        //
        // if (!guisupport)
        //{
        //    QvMessageBoxWarn(this, tr("Unsupported Outbound Type"),
        //                     tr("This outbound entry is not supported by the GUI editor.") + NEWLINE +
        //                         tr("We will launch Json Editor instead."));
        //    JsonEditor w(_out, this);
        //    statusLabel->setText(tr("Opening JSON editor"));
        //    _result = OUTBOUND(w.OpenEditor());
        //    _code = w.result();
        //}
        // else
        //{
        //    OutboundEditor w(_out, this);
        //    statusLabel->setText(tr("Opening default outbound editor."));
        //    _result = w.OpenEditor();
        //    _code = w.result();
        //}
        //
        // if (_code == QDialog::Accepted)
        //{
        //    bool isTagChanged = getTag(_out) != getTag(_result);
        //
        //    if (isTagChanged)
        //    {
        //        auto newTag = getTag(_result);
        //        DEBUG(MODULE_UI, "Outbound tag is changed: " + newTag)
        //        RenameItemTag(RENAME_OUTBOUND, getTag(_out), &newTag);
        //    }
        //
        //    DEBUG(MODULE_UI, "Adding new tag: " + getTag(_result))
        //    outbounds.insert(getTag(_result), _result);
        //    statusLabel->setText(tr("OK"));
        //}
    }
    else
    {
        LOG(MODULE_UI, "Cannot apply 'edit' operation to non-inbound and non-outbound")
    }
}

void RouteEditor::on_domainStrategyCombo_currentIndexChanged(const QString &arg1)
{
    LOADINGCHECK
    domainStrategy = arg1;
}

void RouteEditor::on_defaultOutboundCombo_currentTextChanged(const QString &arg1)
{
    LOADINGCHECK
    defaultOutbound = arg1;
}

void RouteEditor::on_importExistingBtn_clicked()
{
    const auto connId = ConnectionId{ importConnBtn->currentData(Qt::UserRole).toString() };
    const auto root = ConnectionManager->GetConnectionRoot(connId);
    auto outbound = OUTBOUND(root["outbounds"].toArray()[0].toObject());
    outbound["tag"] = GetDisplayName(connId);
    auto _ = nodeDispatcher->CreateOutbound(make_outbound(outbound));
}

void RouteEditor::on_importGroupBtn_currentIndexChanged(int)
{
    const auto group = GroupId{ importGroupBtn->currentData(Qt::UserRole).toString() };
    importConnBtn->clear();
    for (const auto &connId : ConnectionManager->Connections(group))
    {
        importConnBtn->addItem(GetDisplayName(connId), connId.toString());
    }
}

void RouteEditor::on_addBalancerBtn_clicked()
{
    auto _ = nodeDispatcher->CreateOutbound(make_outbound(BalancerId{ GenerateRandomString() }, "Balancer"));
}

void RouteEditor::on_addChainBtn_clicked()
{
    auto _ = nodeDispatcher->CreateOutbound(make_outbound(ChainId{ GenerateRandomString() }, "Chained Outbound"));
}
