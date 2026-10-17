import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

ApplicationWindow {
    id: rootWindow
    visible: true
    width: 1360
    height: 860
    minimumWidth: 1080
    minimumHeight: 700
    title: "RepoDoctor — Open-Source Repository Health Analyzer (Scan. Diagnose. Improve.)"
    color: "#0b101b"

    property int currentTabIndex: 0
    property int selectedFindingIndex: 0
    property int selectedFixIndex: 0
    property string selectedReportFormat: "json"

    readonly property color bgPrimary: "#0b101b"
    readonly property color bgSidebar: "#0f172a"
    readonly property color bgCard: "#131c2e"
    readonly property color bgCardHover: "#18243a"
    readonly property color borderSubtle: "#1e293b"
    readonly property color textPrimary: "#f1f5f9"
    readonly property color textSecondary: "#94a3b8"
    readonly property color accentCyan: "#06b6d4"
    readonly property color accentTeal: "#14b8a6"
    readonly property color statusPass: "#10b981"
    readonly property color statusWarn: "#f59e0b"
    readonly property color statusFail: "#ef4444"

    function severityColor(sev) {
        if (sev === "CRITICAL" || sev === "HIGH" || sev === "FAIL") return statusFail
        if (sev === "MEDIUM" || sev === "WARNING") return statusWarn
        if (sev === "PASS") return statusPass
        return accentCyan
    }

    Connections {
        target: backend
        function onScanResultChanged() {
            if (backend.hasScanResult && !backend.isScanning) rootWindow.currentTabIndex = 1
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ====================================================================
        // Left Navigation Sidebar
        // ====================================================================
        Rectangle {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            color: bgSidebar
            border.color: borderSubtle
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                // Brand Header
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    RowLayout {
                        spacing: 10
                        Rectangle {
                            width: 34
                            height: 34
                            radius: 8
                            color: "#083344"
                            border.color: accentCyan
                            border.width: 1.5
                            Text {
                                anchors.centerIn: parent
                                text: "RD"
                                color: accentCyan
                                font.pixelSize: 15
                                font.bold: true
                            }
                        }
                        ColumnLayout {
                            spacing: 0
                            Text {
                                text: "RepoDoctor"
                                color: textPrimary
                                font.pixelSize: 19
                                font.bold: true
                            }
                            Text {
                                text: "Scan. Diagnose. Improve."
                                color: accentCyan
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: borderSubtle
                    Layout.topMargin: 6
                    Layout.bottomMargin: 6
                }

                Repeater {
                    model: [
                        { label: "Home", subtitle: "Local & GitHub Scan" },
                        { label: "Scan Dashboard", subtitle: "Health Score & Metrics" },
                        { label: "Findings", subtitle: "Evidence & Remediation" },
                        { label: "Auto-Fix", subtitle: "Diff Preview & Apply" },
                        { label: "Reports", subtitle: "JSON, SARIF & HTML" },
                        { label: "Settings", subtitle: "Rules & Exclusions" }
                    ]

                    delegate: Rectangle {
                        Layout.fillWidth: true
                        height: 50
                        radius: 8
                        color: rootWindow.currentTabIndex === index ? "#16263d" : "transparent"
                        border.color: rootWindow.currentTabIndex === index ? accentCyan : "transparent"
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10

                            Rectangle {
                                width: 6
                                height: 24
                                radius: 3
                                color: rootWindow.currentTabIndex === index ? accentCyan : "transparent"
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Text {
                                    text: modelData.label
                                    color: rootWindow.currentTabIndex === index ? textPrimary : textSecondary
                                    font.pixelSize: 14
                                    font.bold: rootWindow.currentTabIndex === index
                                }
                                Text {
                                    text: modelData.subtitle
                                    color: "#64748b"
                                    font.pixelSize: 11
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: rootWindow.currentTabIndex = index
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // Current Scan Status Pill in Sidebar
                Rectangle {
                    Layout.fillWidth: true
                    height: 82
                    radius: 8
                    color: bgCard
                    border.color: borderSubtle
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 4

                        Text {
                            text: backend.hasScanResult ? ("Active: " + backend.currentRepoName) : "No Active Repository"
                            color: textPrimary
                            font.pixelSize: 12
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: backend.hasScanResult
                                  ? ("Score: " + backend.overallScore.toFixed(1) + " / 100  [" + backend.overallGrade + "]")
                                  : "Run a scan from Home"
                            color: backend.hasScanResult ? accentCyan : textSecondary
                            font.pixelSize: 12
                        }
                        Text {
                            text: "100% Local Offline Engine"
                            color: statusPass
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }

        // ====================================================================
        // Main Content Area
        // ====================================================================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Top Status Bar
            Rectangle {
                Layout.fillWidth: true
                height: 48
                color: bgSidebar
                border.color: borderSubtle
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    spacing: 12

                    Rectangle {
                        width: 10
                        height: 10
                        radius: 5
                        color: backend.isScanning ? statusWarn : (backend.hasScanResult ? statusPass : accentCyan)
                    }

                    Text {
                        text: backend.statusMessage
                        color: textSecondary
                        font.pixelSize: 13
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Button {
                        visible: backend.isScanning
                        text: "Cancel Scan"
                        onClicked: backend.cancelScan()
                    }
                }
            }

            // StackLayout for the 6 Pages
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: rootWindow.currentTabIndex

                // ============================================================
                // PAGE 0: HOME
                // ============================================================
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: rootWindow.width - 288
                        x: 24
                        spacing: 20

                        Item { height: 10 }

                        Text {
                            text: "Repository Health Analyzer"
                            color: textPrimary
                            font.pixelSize: 26
                            font.bold: true
                        }
                        Text {
                            text: "Inspect local Git repositories or public GitHub repositories for documentation completeness, Git hygiene, secret exposure, CI/build reliability, and dependency health."
                            color: textSecondary
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        // Dual Scan Cards (Local + Public GitHub)
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 18

                            // Local Repository Card
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 190
                                radius: 10
                                color: bgCard
                                border.color: borderSubtle
                                border.width: 1

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 20
                                    spacing: 10

                                    Text {
                                        text: "Scan Local Git Repository"
                                        color: textPrimary
                                        font.pixelSize: 17
                                        font.bold: true
                                    }
                                    Text {
                                        text: "Analyze any repository on your disk with zero network calls or external uploads."
                                        color: textSecondary
                                        font.pixelSize: 12
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        TextField {
                                            id: localPathInput
                                            Layout.fillWidth: true
                                            text: backend.currentRepoPath !== "" ? backend.currentRepoPath : "."
                                            placeholderText: "/path/to/local/repository"
                                            color: textPrimary
                                            font.pixelSize: 13
                                            background: Rectangle {
                                                color: bgPrimary
                                                radius: 6
                                                border.color: borderSubtle
                                            }
                                        }

                                        Button {
                                            text: "Browse..."
                                            onClicked: {
                                                var chosen = backend.browseForRepositoryDirectory()
                                                if (chosen !== "") {
                                                    localPathInput.text = chosen
                                                }
                                            }
                                        }
                                    }

                                    Button {
                                        text: "Scan Local Repository"
                                        Layout.alignment: Qt.AlignRight
                                        onClicked: {
                                            if (!backend.isScanning) backend.scanLocalPath(localPathInput.text)
                                        }
                                        background: Rectangle {
                                            color: accentCyan
                                            radius: 6
                                            implicitWidth: 180
                                            implicitHeight: 36
                                        }
                                        contentItem: Text {
                                            text: parent.text
                                            color: "#042f2e"
                                            font.bold: true
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                }
                            }

                            // Public GitHub URL Card
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 190
                                radius: 10
                                color: bgCard
                                border.color: borderSubtle
                                border.width: 1

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 20
                                    spacing: 10

                                    Text {
                                        text: "Scan Public GitHub Repository"
                                        color: textPrimary
                                        font.pixelSize: 17
                                        font.bold: true
                                    }
                                    Text {
                                        text: "Performs a safe, shallow, hook-disabled temporary checkout and cleans up automatically."
                                        color: textSecondary
                                        font.pixelSize: 12
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }

                                    TextField {
                                        id: githubUrlInput
                                        Layout.fillWidth: true
                                        placeholderText: "https://github.com/owner/repository"
                                        color: textPrimary
                                        font.pixelSize: 13
                                        background: Rectangle {
                                            color: bgPrimary
                                            radius: 6
                                            border.color: borderSubtle
                                        }
                                    }

                                    Button {
                                        text: "Scan GitHub URL"
                                        Layout.alignment: Qt.AlignRight
                                        onClicked: {
                                            if (!backend.isScanning) backend.scanGitHubUrl(githubUrlInput.text)
                                        }
                                        background: Rectangle {
                                            color: accentTeal
                                            radius: 6
                                            implicitWidth: 165
                                            implicitHeight: 36
                                        }
                                        contentItem: Text {
                                            text: parent.text
                                            color: "#042f2e"
                                            font.bold: true
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                }
                            }
                        }

                        // Quick Start & Recent Scans Row
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 18

                            // Recent Scans Panel
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 270
                                radius: 10
                                color: bgCard
                                border.color: borderSubtle
                                border.width: 1

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 18
                                    spacing: 10

                                    Text {
                                        text: "Recent Scans (" + backend.recentScans.length + ")"
                                        color: textPrimary
                                        font.pixelSize: 16
                                        font.bold: true
                                    }

                                    Text {
                                        visible: backend.recentScans.length === 0
                                        text: "No repositories scanned in this session yet."
                                        color: textSecondary
                                        font.pixelSize: 13
                                    }

                                    ListView {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        clip: true
                                        spacing: 8
                                        model: backend.recentScans

                                        delegate: Rectangle {
                                            width: ListView.view.width
                                            height: 54
                                            radius: 6
                                            color: bgPrimary
                                            border.color: borderSubtle

                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.margins: 10
                                                spacing: 12

                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 2
                                                    Text {
                                                        text: modelData.name + "  (" + modelData.path + ")"
                                                        color: textPrimary
                                                        font.pixelSize: 13
                                                        font.bold: true
                                                        elide: Text.ElideRight
                                                        Layout.fillWidth: true
                                                    }
                                                    Text {
                                                        text: "Scanned: " + modelData.timestamp + " | Findings: " + modelData.findingsCount
                                                        color: textSecondary
                                                        font.pixelSize: 11
                                                    }
                                                }

                                                Rectangle {
                                                    width: 82
                                                    height: 28
                                                    radius: 14
                                                    color: "#083344"
                                                    border.color: accentCyan
                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: modelData.score.toFixed(1) + " [" + modelData.grade + "]"
                                                        color: accentCyan
                                                        font.pixelSize: 12
                                                        font.bold: true
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            // Quick Start Guide Card
                            Rectangle {
                                Layout.preferredWidth: 420
                                Layout.preferredHeight: 270
                                radius: 10
                                color: bgCard
                                border.color: borderSubtle
                                border.width: 1

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 18
                                    spacing: 10

                                    Text {
                                        text: "Quick Start & Security Guarantees"
                                        color: textPrimary
                                        font.pixelSize: 16
                                        font.bold: true
                                    }

                                    Repeater {
                                        model: [
                                            "1. Passive Inspection: Never executes repository scripts or hooks.",
                                            "2. Automatic Redaction: Discovered secrets are masked before storage.",
                                            "3. Explainable Scoring: 0-100 breakdown across 6 engineering pillars.",
                                            "4. Safe Auto-Fix: Preview unified diffs & create backups before writing.",
                                            "5. CI-Ready Export: Export JSON, SARIF 2.1.0, and standalone HTML."
                                        ]
                                        delegate: Text {
                                            text: modelData
                                            color: textSecondary
                                            font.pixelSize: 12
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }
                                    }

                                    Item { Layout.fillHeight: true }
                                }
                            }
                        }
                    }
                }

                // ============================================================
                // PAGE 1: SCAN DASHBOARD
                // ============================================================
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: rootWindow.width - 288
                        x: 24
                        spacing: 18

                        Item { height: 8 }

                        // Top Summary Banner
                        Rectangle {
                            Layout.fillWidth: true
                            height: 150
                            radius: 12
                            color: bgCard
                            border.color: borderSubtle
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 22
                                spacing: 28

                                // Overall Score Badge
                                Rectangle {
                                    width: 106
                                    height: 106
                                    radius: 53
                                    color: "#083344"
                                    border.color: accentCyan
                                    border.width: 3

                                    ColumnLayout {
                                        anchors.centerIn: parent
                                        spacing: 0
                                        Text {
                                            Layout.alignment: Qt.AlignHCenter
                                            text: backend.hasScanResult ? backend.overallScore.toFixed(1) : "--"
                                            color: textPrimary
                                            font.pixelSize: 26
                                            font.bold: true
                                        }
                                        Text {
                                            Layout.alignment: Qt.AlignHCenter
                                            text: backend.hasScanResult ? ("Grade " + backend.overallGrade) : "No Scan"
                                            color: accentCyan
                                            font.pixelSize: 12
                                            font.bold: true
                                        }
                                    }
                                }

                                // Repo Info & Metrics
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    Text {
                                        text: backend.hasScanResult ? backend.currentRepoName : "No Repository Scanned"
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        color: textPrimary
                                        font.pixelSize: 22
                                        font.bold: true
                                    }
                                    Text {
                                        text: backend.hasScanResult ? backend.currentRepoPath : "Select a repository from the Home tab to view real diagnostics."
                                        color: textSecondary
                                        font.pixelSize: 13
                                        elide: Text.ElideMiddle
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: "Duration: " + backend.scanDurationMs.toFixed(1) + " ms  |  Files: " + backend.totalFilesDiscovered + "  |  Text: " + backend.textFilesAnalyzed + "  |  Binary: " + backend.binaryFilesDetected
                                        color: textSecondary
                                        font.pixelSize: 12
                                        wrapMode: Text.WordWrap
                                    }
                                }

                                // Severity Counters
                                RowLayout {
                                    spacing: 10
                                    Repeater {
                                        model: [
                                            { label: "CRITICAL", count: backend.criticalCount, col: statusFail },
                                            { label: "HIGH", count: backend.highCount, col: statusFail },
                                            { label: "MEDIUM", count: backend.mediumCount, col: statusWarn },
                                            { label: "LOW", count: backend.lowCount, col: accentCyan }
                                        ]
                                        delegate: Rectangle {
                                            width: 82
                                            height: 76
                                            radius: 8
                                            color: bgPrimary
                                            border.color: modelData.col
                                            border.width: 1

                                            ColumnLayout {
                                                anchors.centerIn: parent
                                                spacing: 2
                                                Text {
                                                    Layout.alignment: Qt.AlignHCenter
                                                    text: modelData.count
                                                    color: modelData.col
                                                    font.pixelSize: 22
                                                    font.bold: true
                                                }
                                                Text {
                                                    Layout.alignment: Qt.AlignHCenter
                                                    text: modelData.label
                                                    color: textSecondary
                                                    font.pixelSize: 10
                                                    font.bold: true
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Check Status Summary Strip
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Repeater {
                                model: [
                                    { title: "Passed Checks", val: backend.passedChecks, col: statusPass },
                                    { title: "Warnings", val: backend.warningChecks, col: statusWarn },
                                    { title: "Failed Checks", val: backend.failedChecks, col: statusFail },
                                    { title: "Not Applicable", val: backend.notApplicableChecks, col: textSecondary },
                                    { title: "Unverified Checks", val: backend.notVerifiedChecks, col: accentCyan }
                                ]
                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    height: 68
                                    radius: 8
                                    color: bgCard
                                    border.color: borderSubtle

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 14
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            Text {
                                                text: modelData.title
                                                color: textSecondary
                                                font.pixelSize: 12
                                            }
                                            Text {
                                                text: modelData.val
                                                color: modelData.col
                                                font.pixelSize: 20
                                                font.bold: true
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Text {
                            text: "Category Health Breakdown (6 Engineering Pillars)"
                            color: textPrimary
                            font.pixelSize: 18
                            font.bold: true
                        }

                        // 6 Category Cards Grid
                        GridLayout {
                            Layout.fillWidth: true
                            columns: rootWindow.width < 1300 ? 2 : 3
                            rowSpacing: 14
                            columnSpacing: 14

                            Repeater {
                                model: backend.categoryScores
                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 140
                                    radius: 10
                                    color: bgCard
                                    border.color: borderSubtle
                                    border.width: 1

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 16
                                        spacing: 8

                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text {
                                                text: modelData.label
                                                wrapMode: Text.WordWrap
                                                color: textPrimary
                                                font.pixelSize: 15
                                                font.bold: true
                                                Layout.fillWidth: true
                                            }
                                            Rectangle {
                                                Layout.preferredWidth: 122
                                                height: 22
                                                radius: 11
                                                color: "#1e293b"
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: modelData.status
                                                    color: severityColor(modelData.status)
                                                    font.pixelSize: 10
                                                    font.bold: true
                                                }
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text {
                                                text: (modelData.status === "NOT_APPLICABLE" || modelData.status === "NOT_VERIFIED")
                                                      ? modelData.status
                                                      : (modelData.score.toFixed(1) + " / 100")
                                                color: accentCyan
                                                font.pixelSize: 22
                                                font.bold: true
                                            }
                                            Item { Layout.fillWidth: true }
                                            Text {
                                                text: "Weight: " + modelData.weight.toFixed(1) + "%"
                                                color: textSecondary
                                                font.pixelSize: 11
                                            }
                                        }

                                        // Progress Bar
                                        Rectangle {
                                            Layout.fillWidth: true
                                            height: 7
                                            radius: 3.5
                                            color: bgPrimary
                                            Rectangle {
                                                width: parent.width * Math.max(0.0, Math.min(1.0, modelData.score / 100.0))
                                                height: parent.height
                                                radius: 3.5
                                                color: severityColor(modelData.status)
                                            }
                                        }

                                        Text {
                                            text: "Pass: " + modelData.passed + "  |  Warn: " + modelData.warnings + "  |  Fail: " + modelData.failed + "  |  Unverified: " + modelData.notVerified
                                            color: textSecondary
                                            font.pixelSize: 11
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ============================================================
                // PAGE 2: FINDINGS INSPECTOR
                // ============================================================
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 14

                        // Search & Filter Bar
                        Rectangle {
                            Layout.fillWidth: true
                            height: 58
                            radius: 10
                            color: bgCard
                            border.color: borderSubtle

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 12

                                TextField {
                                    id: findingSearchField
                                    Layout.fillWidth: true
                                    placeholderText: "Search findings by rule ID, title, summary, or file path..."
                                    color: textPrimary
                                    font.pixelSize: 13
                                    onTextChanged: backend.updateFindingsFilter(
                                        text, categoryCombo.currentText, severityCombo.currentText, sortCombo.currentText)
                                    background: Rectangle {
                                        color: bgPrimary
                                        radius: 6
                                        border.color: borderSubtle
                                    }
                                }

                                ComboBox {
                                    id: categoryCombo
                                    Layout.preferredWidth: 190
                                    model: ["ALL", "Documentation", "RepositoryHygiene", "SecurityHygiene", "BuildAndCI", "Testing", "DependencyManagement"]
                                    onActivated: backend.updateFindingsFilter(
                                        findingSearchField.text, currentText, severityCombo.currentText, sortCombo.currentText)
                                }

                                ComboBox {
                                    id: severityCombo
                                    Layout.preferredWidth: 140
                                    model: ["ALL", "CRITICAL", "HIGH", "MEDIUM", "LOW", "INFO"]
                                    onActivated: backend.updateFindingsFilter(
                                        findingSearchField.text, categoryCombo.currentText, currentText, sortCombo.currentText)
                                }

                                ComboBox {
                                    id: sortCombo
                                    Layout.preferredWidth: 160
                                    model: ["SEVERITY_DESC", "SEVERITY_ASC", "RULE_ID"]
                                    onActivated: backend.updateFindingsFilter(
                                        findingSearchField.text, categoryCombo.currentText, severityCombo.currentText, currentText)
                                }
                            }
                        }

                        // Master-Detail Split View
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 16

                            // Left Findings List
                            Rectangle {
                                Layout.preferredWidth: 430
                                Layout.fillHeight: true
                                radius: 10
                                color: bgCard
                                border.color: borderSubtle

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 8

                                    Text {
                                        text: "Findings (" + backend.filteredFindings.length + ")"
                                        color: textPrimary
                                        font.pixelSize: 15
                                        font.bold: true
                                    }

                                    ListView {
                                        id: findingsListView
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        clip: true
                                        spacing: 8
                                        model: backend.filteredFindings

                                        delegate: Rectangle {
                                            width: ListView.view.width
                                            height: 78
                                            radius: 8
                                            color: rootWindow.selectedFindingIndex === index ? "#172a45" : bgPrimary
                                            border.color: rootWindow.selectedFindingIndex === index ? accentCyan : borderSubtle

                                            ColumnLayout {
                                                anchors.fill: parent
                                                anchors.margins: 10
                                                spacing: 4

                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 8

                                                    Rectangle {
                                                        width: 72
                                                        height: 20
                                                        radius: 4
                                                        color: severityColor(modelData.severity)
                                                        Text {
                                                            anchors.centerIn: parent
                                                            text: modelData.severity
                                                            color: "#0b101b"
                                                            font.pixelSize: 10
                                                            font.bold: true
                                                        }
                                                    }

                                                    Text {
                                                        text: modelData.ruleId
                                                        color: accentCyan
                                                        font.pixelSize: 12
                                                        font.bold: true
                                                    }

                                                    Item { Layout.fillWidth: true }

                                                    Text {
                                                        visible: modelData.fixable
                                                        text: "[Auto-Fixable]"
                                                        color: statusPass
                                                        font.pixelSize: 11
                                                        font.bold: true
                                                    }
                                                }

                                                Text {
                                                    text: modelData.title
                                                    color: textPrimary
                                                    font.pixelSize: 13
                                                    font.bold: true
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                }

                                                Text {
                                                    text: modelData.categoryLabel + " • " + modelData.primaryFile
                                                    color: textSecondary
                                                    font.pixelSize: 11
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                }
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: rootWindow.selectedFindingIndex = index
                                            }
                                        }
                                    }
                                }
                            }

                            // Right Finding Detail & Evidence Inspector
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 10
                                color: bgCard
                                border.color: borderSubtle

                                ColumnLayout {
                                    id: detailCol
                                    anchors.fill: parent
                                    anchors.margins: 20
                                    spacing: 12

                                    property var activeFinding: (backend.filteredFindings.length > 0 && rootWindow.selectedFindingIndex < backend.filteredFindings.length)
                                                                ? backend.filteredFindings[rootWindow.selectedFindingIndex]
                                                                : null

                                    Text {
                                        visible: detailCol.activeFinding === null
                                        text: backend.hasScanResult
                                              ? "Zero findings match the current filter criteria. All applicable checks passed!"
                                              : "Run a repository scan to inspect findings and evidence."
                                        color: statusPass
                                        font.pixelSize: 15
                                        font.bold: true
                                    }

                                    ColumnLayout {
                                        visible: detailCol.activeFinding !== null
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        spacing: 10

                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text {
                                                text: detailCol.activeFinding ? (detailCol.activeFinding.ruleId + " — " + detailCol.activeFinding.title) : ""
                                                color: textPrimary
                                                font.pixelSize: 18
                                                font.bold: true
                                                wrapMode: Text.WordWrap
                                                Layout.fillWidth: true
                                            }
                                            Button {
                                                text: "Open File Location"
                                                onClicked: {
                                                    if (detailCol.activeFinding) {
                                                        backend.openFileLocation(detailCol.activeFinding.primaryFile)
                                                    }
                                                }
                                            }
                                        }

                                        Text {
                                            text: detailCol.activeFinding ? detailCol.activeFinding.summary : ""
                                            color: accentCyan
                                            font.pixelSize: 14
                                            font.bold: true
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            text: detailCol.activeFinding ? ("Rule Description: " + detailCol.activeFinding.description) : ""
                                            color: textSecondary
                                            font.pixelSize: 13
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            text: "Redacted Evidence (" + (detailCol.activeFinding ? detailCol.activeFinding.evidence.length : 0) + "):"
                                            color: textPrimary
                                            font.pixelSize: 14
                                            font.bold: true
                                        }

                                        ListView {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 170
                                            clip: true
                                            spacing: 6
                                            model: detailCol.activeFinding ? detailCol.activeFinding.evidence : []

                                            delegate: Rectangle {
                                                width: ListView.view.width
                                                height: 58
                                                radius: 6
                                                color: bgPrimary
                                                border.color: borderSubtle

                                                ColumnLayout {
                                                    anchors.fill: parent
                                                    anchors.margins: 8
                                                    spacing: 2

                                                    Text {
                                                        text: modelData.file + (modelData.line > 0 ? (":" + modelData.line) : "") + (modelData.isRedacted ? "  [REDACTED SECRET]" : "")
                                                        color: modelData.isRedacted ? statusWarn : accentCyan
                                                        font.pixelSize: 12
                                                        font.bold: true
                                                    }
                                                    Text {
                                                        text: modelData.snippet + (modelData.context !== "" ? ("  — " + modelData.context) : "")
                                                        color: textPrimary
                                                        font.family: "Consolas"
                                                        font.pixelSize: 12
                                                        elide: Text.ElideRight
                                                        Layout.fillWidth: true
                                                    }
                                                }
                                            }
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            height: 92
                                            radius: 8
                                            color: "#091926"
                                            border.color: accentTeal

                                            ColumnLayout {
                                                anchors.fill: parent
                                                anchors.margins: 10
                                                spacing: 4
                                                Text {
                                                    text: "Actionable Remediation Guidance:"
                                                    color: accentTeal
                                                    font.pixelSize: 13
                                                    font.bold: true
                                                }
                                                Text {
                                                    text: detailCol.activeFinding ? detailCol.activeFinding.remediation : ""
                                                    color: textPrimary
                                                    font.pixelSize: 12
                                                    wrapMode: Text.WordWrap
                                                    Layout.fillWidth: true
                                                }
                                            }
                                        }

                                        Text {
                                            text: detailCol.activeFinding ? ("False-Positive Considerations: " + detailCol.activeFinding.falsePositives) : ""
                                            color: textSecondary
                                            font.pixelSize: 12
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }

                                        Item { Layout.fillHeight: true }
                                    }
                                }
                            }
                        }
                    }
                }

                // ============================================================
                // PAGE 3: SAFE AUTO-FIX PLANNER & UNIFIED DIFF PREVIEW
                // ============================================================
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 20
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: "Safe Auto-Fix Planner & Unified Diff Preview"
                                color: textPrimary
                                font.pixelSize: 20
                                font.bold: true
                            }
                            Text {
                                text: "Preview every planned file change as a unified diff. Backups are created in .repodoctor-backup/ before any modification."
                                color: textSecondary
                                font.pixelSize: 13
                            }
                        }

                        Button {
                            text: "Dry-Run Validate Selected"
                            onClicked: backend.executeSelectedFixes(true)
                        }

                        Button {
                            text: "Apply Selected Fixes (with Backup)"
                            onClicked: confirmFixPopup.open()
                            background: Rectangle {
                                color: accentTeal
                                radius: 6
                                implicitWidth: 230
                                implicitHeight: 36
                            }
                            contentItem: Text {
                                text: parent.text
                                color: "#042f2e"
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 16

                        // Planned Fixes List
                        Rectangle {
                            Layout.preferredWidth: 440
                            Layout.fillHeight: true
                            radius: 10
                            color: bgCard
                            border.color: borderSubtle

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                Text {
                                    text: "Available Safe Fixes (" + backend.plannedFixes.length + ")"
                                    color: textPrimary
                                    font.pixelSize: 15
                                    font.bold: true
                                }

                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    spacing: 8
                                    model: backend.plannedFixes

                                    delegate: Rectangle {
                                        width: ListView.view.width
                                        height: 82
                                        radius: 8
                                        color: rootWindow.selectedFixIndex === index ? "#172a45" : bgPrimary
                                        border.color: rootWindow.selectedFixIndex === index ? accentCyan : borderSubtle

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            spacing: 10

                                            CheckBox {
                                                checked: modelData.selected
                                                enabled: !modelData.hasConflict
                                                onToggled: backend.toggleFixSelection(modelData.fixId, checked)
                                            }

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 3
                                                Text {
                                                    text: modelData.title
                                                    color: textPrimary
                                                    font.pixelSize: 13
                                                    font.bold: true
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                }
                                                Text {
                                                    text: "Target: " + modelData.targetPath + " (" + modelData.ruleId + ")"
                                                    color: accentCyan
                                                    font.pixelSize: 11
                                                }
                                                Text {
                                                    visible: modelData.hasConflict
                                                    text: "CONFLICT: " + modelData.conflictReason
                                                    color: statusFail
                                                    font.pixelSize: 11
                                                }
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            anchors.leftMargin: 44
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: rootWindow.selectedFixIndex = index
                                        }
                                    }
                                }
                            }
                        }

                        // Unified Diff Preview & Execution Log
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 12

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 10
                                color: bgCard
                                border.color: borderSubtle

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 8

                                    Text {
                                        text: "Unified Diff Preview"
                                        color: textPrimary
                                        font.pixelSize: 15
                                        font.bold: true
                                    }

                                    ScrollView {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        clip: true

                                        TextArea {
                                            readOnly: true
                                            text: (backend.plannedFixes.length > 0 && rootWindow.selectedFixIndex < backend.plannedFixes.length)
                                                  ? backend.plannedFixes[rootWindow.selectedFixIndex].unifiedDiff
                                                  : "No planned fixes needed for the current repository."
                                            color: "#a7f3d0"
                                            font.family: "Consolas"
                                            font.pixelSize: 13
                                            background: Rectangle {
                                                color: "#060a12"
                                                radius: 6
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 140
                                radius: 10
                                color: bgCard
                                border.color: borderSubtle

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 6

                                    Text {
                                        text: "Execution Log (" + backend.fixExecutionLog.length + ")"
                                        color: textPrimary
                                        font.pixelSize: 14
                                        font.bold: true
                                    }

                                    ListView {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        clip: true
                                        model: backend.fixExecutionLog
                                        delegate: Text {
                                            text: "[" + modelData.fixId + "] " + modelData.message + (modelData.backupPath !== "" ? (" (Backup: " + modelData.backupPath + ")") : "")
                                            color: modelData.applied ? statusPass : accentCyan
                                            font.pixelSize: 12
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ============================================================
                // PAGE 4: REPORTS & EXPORT
                // ============================================================
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 20
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Text {
                            text: "Reports & CI Export (JSON / SARIF 2.1.0 / HTML)"
                            color: textPrimary
                            font.pixelSize: 20
                            font.bold: true
                            Layout.fillWidth: true
                        }

                        Repeater {
                            model: ["json", "sarif", "html", "console"]
                            delegate: Button {
                                text: "Preview " + modelData.toUpperCase()
                                onClicked: {
                                    rootWindow.selectedReportFormat = modelData
                                    backend.generateReportPreview(modelData)
                                }
                            }
                        }

                        Button {
                            text: "Export " + rootWindow.selectedReportFormat.toUpperCase() + " Report..."
                            onClicked: {
                                var ext = rootWindow.selectedReportFormat === "console" ? "txt" : rootWindow.selectedReportFormat
                                var path = backend.browseSaveReportPath("repodoctor-report." + ext)
                                if (path !== "") {
                                    backend.exportReportToFile(rootWindow.selectedReportFormat, path)
                                }
                            }
                            background: Rectangle {
                                color: accentCyan
                                radius: 6
                                implicitWidth: 190
                                implicitHeight: 36
                            }
                            contentItem: Text {
                                text: parent.text
                                color: "#042f2e"
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 16

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 10
                            color: bgCard
                            border.color: borderSubtle

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 8

                                Text {
                                    text: "Live Report Preview (" + rootWindow.selectedReportFormat.toUpperCase() + ")"
                                    color: textPrimary
                                    font.pixelSize: 15
                                    font.bold: true
                                }

                                ScrollView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true

                                    TextArea {
                                        readOnly: true
                                        text: backend.reportPreviewText
                                        color: textPrimary
                                        font.family: "Consolas"
                                        font.pixelSize: 12
                                        background: Rectangle {
                                            color: "#060a12"
                                            radius: 6
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 360
                            Layout.fillHeight: true
                            radius: 10
                            color: bgCard
                            border.color: borderSubtle

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 8

                                Text {
                                    text: "Export History (" + backend.exportHistory.length + ")"
                                    color: textPrimary
                                    font.pixelSize: 15
                                    font.bold: true
                                }

                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    spacing: 8
                                    model: backend.exportHistory
                                    delegate: Rectangle {
                                        width: ListView.view.width
                                        height: 64
                                        radius: 6
                                        color: bgPrimary
                                        border.color: borderSubtle
                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            spacing: 2
                                            Text {
                                                text: modelData.format + " — " + modelData.status
                                                color: accentCyan
                                                font.pixelSize: 12
                                                font.bold: true
                                            }
                                            Text {
                                                text: modelData.path
                                                color: textPrimary
                                                font.pixelSize: 11
                                                elide: Text.ElideMiddle
                                                Layout.fillWidth: true
                                            }
                                            Text {
                                                text: modelData.timestamp
                                                color: textSecondary
                                                font.pixelSize: 10
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ============================================================
                // PAGE 5: SETTINGS (RULES, EXCLUSIONS, LIMITS & PRIVACY)
                // ============================================================
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 20
                    spacing: 18

                    // Left Rules Configuration Table
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 10
                        color: bgCard
                        border.color: borderSubtle

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 10

                            Text {
                                text: "Diagnostic Rules Catalog (" + backend.rulesCatalog.length + " Rules)"
                                color: textPrimary
                                font.pixelSize: 17
                                font.bold: true
                            }

                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                spacing: 6
                                model: backend.rulesCatalog

                                delegate: Rectangle {
                                    width: ListView.view.width
                                    height: 62
                                    radius: 6
                                    color: bgPrimary
                                    border.color: borderSubtle

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 10

                                        CheckBox {
                                            checked: modelData.enabled
                                            onToggled: backend.setRuleEnabled(modelData.ruleId, checked, "Updated via GUI Settings")
                                        }

                                        Text {
                                            text: modelData.ruleId
                                            color: accentCyan
                                            font.pixelSize: 12
                                            font.bold: true
                                            Layout.preferredWidth: 95
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            Text {
                                                text: modelData.title
                                                color: textPrimary
                                                font.pixelSize: 13
                                                font.bold: true
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                            Text {
                                                text: modelData.category + " • Severity: " + modelData.severity
                                                color: textSecondary
                                                font.pixelSize: 11
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Right Exclusions, Limits & Privacy Controls
                    Rectangle {
                        Layout.preferredWidth: 420
                        Layout.fillHeight: true
                        radius: 10
                        color: bgCard
                        border.color: borderSubtle

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 14

                            Text {
                                text: "Scan Limits, Exclusions & Privacy"
                                color: textPrimary
                                font.pixelSize: 17
                                font.bold: true
                            }

                            Text {
                                text: "Excluded Glob Patterns (comma-separated):"
                                color: textSecondary
                                font.pixelSize: 12
                            }
                            TextField {
                                Layout.fillWidth: true
                                text: backend.excludePatternsText
                                onEditingFinished: backend.excludePatternsText = text
                                color: textPrimary
                                background: Rectangle {
                                    color: bgPrimary
                                    radius: 6
                                    border.color: borderSubtle
                                }
                            }

                            Text {
                                text: "Max Text File Size to Inspect (KB): " + backend.maxFileSizeKb
                                color: textSecondary
                                font.pixelSize: 12
                            }
                            Slider {
                                Layout.fillWidth: true
                                from: 256
                                to: 16384
                                stepSize: 256
                                value: backend.maxFileSizeKb
                                onMoved: backend.maxFileSizeKb = Math.round(value)
                            }

                            Text {
                                text: "Large Committed File Warning Threshold (MB): " + backend.largeFileThresholdMb
                                color: textSecondary
                                font.pixelSize: 12
                            }
                            Slider {
                                Layout.fillWidth: true
                                from: 1
                                to: 50
                                stepSize: 1
                                value: backend.largeFileThresholdMb
                                onMoved: backend.largeFileThresholdMb = Math.round(value)
                            }

                            CheckBox {
                                checked: backend.respectGitignore
                                text: "Skip untracked files matched by .gitignore"
                                onToggled: backend.respectGitignore = checked
                                contentItem: Text {
                                    text: parent.text
                                    color: textPrimary
                                    font.pixelSize: 13
                                    leftPadding: parent.indicator.width + 8
                                }
                            }

                            CheckBox {
                                checked: backend.enableAdvisoryLookup
                                text: "Enable local OSV advisory cache (.repodoctor-advisories.json)"
                                onToggled: backend.enableAdvisoryLookup = checked
                                contentItem: Text {
                                    text: parent.text
                                    color: textPrimary
                                    font.pixelSize: 13
                                    leftPadding: parent.indicator.width + 8
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: 90
                                radius: 8
                                color: "#091926"
                                border.color: accentCyan

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 4
                                    Text {
                                        text: "Privacy & Security Controls"
                                        color: accentCyan
                                        font.pixelSize: 13
                                        font.bold: true
                                    }
                                    Text {
                                        text: "RepoDoctor never uploads repository contents, telemetry, or discovered secrets to any external server."
                                        color: textSecondary
                                        font.pixelSize: 11
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                }
                            }

                            Button {
                                text: "Save .repodoctor.json to Active Repository"
                                Layout.fillWidth: true
                                onClicked: backend.saveConfigurationToRepo()
                            }

                            Item { Layout.fillHeight: true }
                        }
                    }
                }
            }
        }
    }

    // Explicit User Confirmation Modal for Auto-Fix Application
    Popup {
        id: confirmFixPopup
        anchors.centerIn: parent
        width: 460
        height: 210
        modal: true
        focus: true
        background: Rectangle {
            color: bgCard
            radius: 10
            border.color: accentCyan
            border.width: 2
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            Text {
                text: "Confirm Applying Selected Auto-Fixes"
                color: textPrimary
                font.pixelSize: 17
                font.bold: true
            }
            Text {
                text: "RepoDoctor will apply the selected non-conflicting fixes to the repository and store backups of modified files in .repodoctor-backup/. Proceed?"
                color: textSecondary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 12
                Button {
                    text: "Cancel"
                    onClicked: confirmFixPopup.close()
                }
                Button {
                    text: "Confirm & Apply Fixes"
                    onClicked: {
                        confirmFixPopup.close()
                        backend.executeSelectedFixes(false)
                    }
                }
            }
        }
    }
}
