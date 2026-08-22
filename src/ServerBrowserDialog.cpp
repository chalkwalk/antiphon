#include "ServerBrowserDialog.h"

static const ServerBrowserDialog::ServerEntry kStaticServers[] = {
    {"ninbot.com", 2049},
    {"ninjam.com", 2049},
    {"autojam.com", 2049},
};

ServerBrowserDialog::ServerBrowserDialog() : table("Servers", this) {
  // The dialog is an overlay child of the editor, not a modal window. It must
  // both accept keyboard focus itself and swallow clicks, or focus and mouse
  // events leak through to the mixer behind it.
  setWantsKeyboardFocus(true);
  setTitle("Connect to a Ninjam server");
  setDescription("Choose a server, enter your nickname, and connect");
  setFocusContainerType(juce::Component::FocusContainerType::focusContainer);
  setInterceptsMouseClicks(true, true);

  closeButton.onClick = [this]() { dismiss(); };
  closeButton.setTitle("Close");
  closeButton.setDescription("Close this dialog");
  addAndMakeVisible(closeButton);

  table.getHeader().addColumn("Server", 1, 230);
  table.getHeader().addColumn("BPM", 2, 55);
  table.getHeader().addColumn("BPI", 3, 55);
  table.getHeader().addColumn("Players", 4, 310);
  table.setHeaderHeight(28);
  table.setRowHeight(24);
  table.setColour(juce::TableListBox::backgroundColourId,
                  juce::Colour(0xff111122));
  table.setOutlineThickness(1);
  table.setTitle("Server list");
  table.setDescription("Public servers, with their tempo and who is playing");
  addAndMakeVisible(table);

  hostInput.setText("ninbot.com");
  hostInput.setName("hostInput");
  hostInput.setTitle("Server address");
  hostInput.setDescription("Host name or IP address of the Ninjam server");
  addAndMakeVisible(hostInput);

  portInput.setText("2049");
  portInput.setInputRestrictions(5, "0123456789");
  portInput.setName("portInput");
  portInput.setTitle("Port");
  portInput.setDescription("Server port. 2049 is the usual one.");
  addAndMakeVisible(portInput);

  usernameInput.setTextToShowWhenEmpty("Nickname", juce::Colours::grey);
  usernameInput.setName("usernameInput");
  usernameInput.setTitle("Nickname");
  usernameInput.setDescription("The name other players will see");
  addAndMakeVisible(usernameInput);

  passwordInput.setPasswordCharacter('*');
  passwordInput.setTextToShowWhenEmpty("Password", juce::Colours::grey);
  passwordInput.setName("passwordInput");
  passwordInput.setTitle("Password");
  passwordInput.setDescription("Only needed when Anonymous is unticked");
  addChildComponent(passwordInput);

  anonymousToggle.setToggleState(true, juce::dontSendNotification);
  anonymousToggle.onClick = [this]() {
    passwordInput.setVisible(!anonymousToggle.getToggleState());
  };
  anonymousToggle.setTitle("Anonymous");
  anonymousToggle.setDescription(
      "Connect without a password, where the server allows it");
  addAndMakeVisible(anonymousToggle);

  connectButton.onClick = [this]() {
    juce::String host = hostInput.getText().trim();
    int port = portInput.getText().getIntValue();
    if (port <= 0)
      port = 2049;
    juce::String nick = usernameInput.getText().trim();
    juce::String user, pass;
    if (anonymousToggle.getToggleState()) {
      user = (nick.isEmpty() || nick.startsWithIgnoreCase("anonymous"))
                 ? (nick.isEmpty() ? "anonymous" : nick)
                 : "anonymous:" + nick;
    } else {
      user = nick;
      pass = passwordInput.getText();
    }
    if (onConnect)
      onConnect(host, port, user, pass);
    dismiss();
  };
  connectButton.setTitle("Connect");
  connectButton.setDescription("Join the server with these details");
  addAndMakeVisible(connectButton);

  practiceButton.onClick = [this]() {
    if (onPractice)
      onPractice();
  };
  practiceButton.setTitle("Practice room");
  practiceButton.setDescription(
      "Start a practice room on this machine and join it: a band of bots you "
      "can play with, and talk to in chat");
  practiceButton.setTooltip(
      "A band on your own machine. Nothing leaves it, and everything works as "
      "it does in a real room -- phase, stems, chat and recording.");
  addAndMakeVisible(practiceButton);

  cancelButton.onClick = [this]() { dismiss(); };
  cancelButton.setTitle("Cancel");
  cancelButton.setDescription("Close without connecting");
  addAndMakeVisible(cancelButton);

  statusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
  statusLabel.setText("Fetching server list...", juce::dontSendNotification);
  statusLabel.setTitle("Browser status");
  statusLabel.setDescription("Progress of the server list fetch");
  addAndMakeVisible(statusLabel);

  populateStaticList();
  fetchThread = std::thread([this]() { doFetch(); });
}

ServerBrowserDialog::~ServerBrowserDialog() {
  stopFetch.store(true);
  fetchSocket.close();
  if (fetchThread.joinable())
    fetchThread.join();
}

void ServerBrowserDialog::dismiss() {
  // Never close from inside a button's own click callback. Every caller here is
  // Button::internalClickCallback, which goes on touching the button after we
  // return -- and closing destroys this component, and the button with it.
  // Unwinding first is what turns a use-after-free into an ordinary close.
  //
  // The callback is copied rather than captured through `this` for the same
  // reason: by the time the lambda runs, this object may be gone.
  if (onClose)
    juce::MessageManager::callAsync([cb = onClose] { cb(); });
}

void ServerBrowserDialog::populateStaticList() {
  servers.clear();
  for (auto &s : kStaticServers)
    servers.add(s);
}

juce::Array<ServerBrowserDialog::ServerEntry>
ServerBrowserDialog::parseServersJSON(const juce::String &json) {
  juce::Array<ServerEntry> result;
  juce::var parsed = juce::JSON::parse(json);

  // API returns {"servers": [...]}; all numeric fields are quoted strings.
  juce::var arr = parsed["servers"];
  if (!arr.isArray())
    return result;

  for (int i = 0; i < arr.size(); ++i) {
    const auto &obj = arr[i];
    ServerEntry e;
    e.host = obj["host"].toString();
    e.port = obj["port"].toString().getIntValue();
    e.bpm = obj["bpm"].toString().getIntValue();
    e.bpi = obj["bpi"].toString().getIntValue();

    const auto &users = obj["users"];
    if (users.isArray() && users.size() > 0) {
      juce::StringArray names;
      for (int j = 0; j < users.size(); ++j)
        names.add(users[j]["name"].toString());
      e.players = names.joinIntoString(", ");
    } else {
      int count = obj["user_count"].toString().getIntValue();
      if (count > 0)
        e.players = juce::String(count) + (count == 1 ? " player" : " players");
    }

    if (e.host.isNotEmpty())
      result.add(e);
  }
  return result;
}

void ServerBrowserDialog::doFetch() {
  if (!fetchSocket.connect("ninbot.com", 80, 3000) || stopFetch.load()) {
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<ServerBrowserDialog>(this)]() {
          if (safe)
            safe->statusLabel.setText(
                "Could not reach ninbot.com -- showing known servers.",
                juce::dontSendNotification);
        });
    return;
  }

  const juce::String req =
      "GET /app/servers.php HTTP/1.0\r\nHost: ninbot.com\r\nConnection: "
      "close\r\n\r\n";
  fetchSocket.write(req.toRawUTF8(), (int)req.getNumBytesAsUTF8());

  juce::MemoryBlock response;
  char buf[4096];
  while (!stopFetch.load()) {
    int ready = fetchSocket.waitUntilReady(true, 200);
    if (ready < 0)
      break;
    if (ready == 0)
      continue;
    int n = fetchSocket.read(buf, sizeof(buf), false);
    if (n <= 0)
      break;
    response.append(buf, n);
  }

  if (stopFetch.load() || response.getSize() == 0)
    return;

  juce::String text = juce::String::fromUTF8(
      static_cast<const char *>(response.getData()), (int)response.getSize());
  int bodyStart = text.indexOf("\r\n\r\n");
  if (bodyStart < 0)
    return;

  auto entries = parseServersJSON(text.substring(bodyStart + 4));

  juce::MessageManager::callAsync(
      [safe = juce::Component::SafePointer<ServerBrowserDialog>(this),
       entries = std::move(entries)]() mutable {
        if (!safe)
          return;
        if (entries.isEmpty()) {
          safe->statusLabel.setText("No live data -- showing known servers.",
                                    juce::dontSendNotification);
        } else {
          safe->servers = entries;
          safe->table.updateContent();
          safe->statusLabel.setText("", juce::dontSendNotification);
        }
      });
}

void ServerBrowserDialog::paint(juce::Graphics &g) {
  // Overall background + border
  g.fillAll(juce::Colour(0xff0d0d1a));
  g.setColour(juce::Colour(0xff00b4d8));
  g.drawRect(getLocalBounds(), 1);

  // Title bar
  auto titleBar = getLocalBounds().removeFromTop(kTitleBarH);
  g.setColour(juce::Colour(0xff111122));
  g.fillRect(titleBar);
  g.setColour(juce::Colour(0xff00b4d8));
  g.drawLine(0.0f, (float)kTitleBarH, (float)getWidth(), (float)kTitleBarH,
             1.0f);

  g.setFont(juce::FontOptions{}.withHeight(14.0f).withStyle("Bold"));
  g.setColour(juce::Colours::white);
  g.drawFittedText("Server Browser",
                   titleBar.reduced(8, 0).withTrimmedRight(kTitleBarH),
                   juce::Justification::centredLeft, 1);
}

void ServerBrowserDialog::setStatus(const juce::String &text) {
  statusLabel.setText(text, juce::dontSendNotification);
}

void ServerBrowserDialog::resized() {
  auto area = getLocalBounds().reduced(1); // inside border

  // Title bar row -- closeButton sits in it
  auto titleBar = area.removeFromTop(kTitleBarH);
  closeButton.setBounds(titleBar.removeFromRight(kTitleBarH).reduced(4));

  area.reduce(9, 9); // inner padding

  // Status label at very bottom
  statusLabel.setBounds(area.removeFromBottom(20));
  area.removeFromBottom(4);

  // Button row
  auto btnRow = area.removeFromBottom(28);
  cancelButton.setBounds(btnRow.removeFromRight(90).reduced(0, 3));
  btnRow.removeFromRight(8);
  connectButton.setBounds(btnRow.removeFromRight(90).reduced(0, 3));
  // Left, away from Connect/Cancel: it is a different destination rather than
  // a different way of pressing the same one.
  practiceButton.setBounds(btnRow.removeFromLeft(130).reduced(0, 3));

  area.removeFromBottom(8);

  // Credentials row
  auto credRow = area.removeFromBottom(26);
  hostInput.setBounds(credRow.removeFromLeft(180).reduced(0, 3));
  credRow.removeFromLeft(6);
  portInput.setBounds(credRow.removeFromLeft(55).reduced(0, 3));
  credRow.removeFromLeft(10);
  usernameInput.setBounds(credRow.removeFromLeft(130).reduced(0, 3));
  credRow.removeFromLeft(8);
  anonymousToggle.setBounds(credRow.removeFromLeft(100).reduced(0, 3));
  credRow.removeFromLeft(6);
  passwordInput.setBounds(credRow.removeFromLeft(130).reduced(0, 3));

  area.removeFromBottom(8);

  // Table fills remaining space
  table.setBounds(area);
}

// --- TableListBoxModel ---

int ServerBrowserDialog::getNumRows() { return servers.size(); }

void ServerBrowserDialog::paintRowBackground(juce::Graphics &g, int /*row*/,
                                             int /*w*/, int /*h*/,
                                             bool selected) {
  g.fillAll(selected ? juce::Colour(0xff00b4d8).withAlpha(0.30f)
                     : juce::Colour(0xff111122));
}

void ServerBrowserDialog::paintCell(juce::Graphics & /*g*/, int /*row*/,
                                    int /*col*/, int /*w*/, int /*h*/,
                                    bool /*selected*/) {
  // Cell text and accessibility attributes are rendered by cell label components
  // in refreshComponentForCell.
}

juce::Component *ServerBrowserDialog::refreshComponentForCell(
    int rowNumber, int columnId, bool /*isRowSelected*/,
    juce::Component *existingComponentToUpdate) {
  auto *label = dynamic_cast<juce::Label *>(existingComponentToUpdate);
  if (label == nullptr) {
    delete existingComponentToUpdate;
    label = new juce::Label();
    label->setFont(juce::FontOptions{}.withHeight(13.0f));
    label->setColour(juce::Label::textColourId, juce::Colours::white);
    label->setJustificationType(juce::Justification::centredLeft);
    label->setInterceptsMouseClicks(false, false);
  }

  if (rowNumber >= servers.size()) {
    label->setText("", juce::dontSendNotification);
    label->setTitle("");
    label->setDescription("");
    return label;
  }

  const auto &s = servers.getReference(rowNumber);
  juce::String text, title, desc;
  switch (columnId) {
  case 1:
    text = s.host + ":" + juce::String(s.port);
    title = "Server: " + s.host + " port " + juce::String(s.port);
    desc = "Server address " + s.host + " on port " + juce::String(s.port);
    break;
  case 2:
    text = s.bpm > 0 ? juce::String(s.bpm) : "-";
    title =
        "BPM: " + (s.bpm > 0 ? juce::String(s.bpm) : juce::String("unknown"));
    desc = "Tempo in beats per minute";
    break;
  case 3:
    text = s.bpi > 0 ? juce::String(s.bpi) : "-";
    title =
        "BPI: " + (s.bpi > 0 ? juce::String(s.bpi) : juce::String("unknown"));
    desc = "Beats per interval";
    break;
  case 4:
    text = s.players;
    title = "Players: " +
            (s.players.isNotEmpty() ? s.players : juce::String("none"));
    desc = "Active players in server";
    break;
  default:
    break;
  }

  label->setText(text, juce::dontSendNotification);
  label->setTitle("");
  label->setDescription(desc);
  return label;
}

void ServerBrowserDialog::selectedRowsChanged(int lastRowSelected) {
  if (lastRowSelected >= 0 && lastRowSelected < servers.size()) {
    const auto &s = servers.getReference(lastRowSelected);
    hostInput.setText(s.host, juce::dontSendNotification);
    portInput.setText(juce::String(s.port), juce::dontSendNotification);
  }
}

void ServerBrowserDialog::cellClicked(int row, int /*col*/,
                                      const juce::MouseEvent &) {
  selectedRowsChanged(row);
}

void ServerBrowserDialog::cellDoubleClicked(int row, int /*col*/,
                                            const juce::MouseEvent &) {
  selectedRowsChanged(row);
  connectButton.triggerClick();
}

void ServerBrowserDialog::returnKeyPressed(int lastRowSelected) {
  if (lastRowSelected >= 0 && lastRowSelected < servers.size()) {
    selectedRowsChanged(lastRowSelected);
    connectButton.triggerClick();
  }
}
