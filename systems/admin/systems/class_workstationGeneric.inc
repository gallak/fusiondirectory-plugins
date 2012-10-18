<?php

/*
  This code is part of FusionDirectory (http://www.fusiondirectory.org/)
  Copyright (C) 2003-2010  Cajus Pollmeier
  Copyright (C) 2011  FusionDirectory

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
*/

class NetworkSettingsDialog
{
  private $attribute;
  private $display;

  function __construct (&$attribute, &$display)
  {
    $this->attribute  = &$attribute;
    $this->display    = &$display;
  }

  function execute ()
  {
    $this->display = $this->attribute->netConfigDNS->execute();
    if (is_object($this->attribute->netConfigDNS->dialog)) {
      return $this->display;
    }
    return FALSE;
  }
}

class NetworkSettingsAttribute extends Attribute
{
  public $netConfigDNS;
  private $display;

  function __construct ()
  {
    parent::__construct("Network Settings", "", "networkSettings", FALSE, "");
    $this->setInLdap(FALSE);
  }

  function setManagedAttributes ($dontcare)
  {
    trigger_error('method setManagedAttributes is not supported for BaseSelectorAttribute');
  }

  function setParent (&$plugin)
  {
    parent::setParent($plugin);
    if (is_object($this->plugin)) {
      $this->plugin->netConfigDNS =& $this->netConfigDNS;
      $this->netConfigDNS = new networkSettings($this->plugin->config, $this->plugin, $this->plugin->objectclasses, TRUE);
      $this->netConfigDNS->MACisMust = TRUE;
      $this->display = $this->netConfigDNS->execute();
    }
  }

  function renderAttribute(&$attributes)
  {
    $attributes[$this->getLdapName()] = array(
      'DNSenabled'  => $this->netConfigDNS->DNSenabled,
      'display'     => $this->display
    );
  }

  function loadPostValue ()
  {
  }

  function applyPostValue ()
  {
    if (!$this->disabled) {
      /* Refresh base */
      $this->netConfigDNS->save_object();
      $this->display = $this->netConfigDNS->execute();
      if (is_object($this->netConfigDNS->dialog)) {
        $this->plugin->openDialog(new NetworkSettingsDialog($this, $this->display));
      }
    }
  }

  function check ()
  {
    $error = parent::check();
    if (!empty($error)) {
      return $error;
    } else {
      return $this->netConfigDNS->check();
    }
  }
}

class ActionAttribute extends SelectAttribute
{
  function __construct ($label, $description, $ldapName, $required = FALSE)
  {
    parent::__construct($label, $description, $ldapName, $required);
    $this->setInLdap(FALSE);
  }

  function setParent (&$plugin)
  {
    parent::setParent($plugin);
    if (is_object($this->plugin)) {
      $this->updateChoices();
    }
  }

  function loadPostValue ()
  {
    $id = $this->getHtmlId();
    if (isset($_POST[$id.'_action'])) {
      parent::loadPostValue();
    }
  }

  function applyPostValue ()
  {
    if (!$this->disabled && $this->postValue != '') {
      $this->updateChoices();
      if (in_array($this->postValue, $this->choices)) {
        $action = $this->postValue;

        /* Check if we have an DaemonEvent for this action */
        if (class_available("supportDaemon") && class_available("DaemonEvent_".$action)) {
          $events = DaemonEvent::get_event_types(SYSTEM_EVENT | HIDDEN_EVENT);
          if (isset($events['TRIGGERED']["DaemonEvent_".$action])) {
            $evt = $events['TRIGGERED']["DaemonEvent_".$action];
            $tmp = new $evt['CLASS_NAME']($this->config);
            $tmp->add_targets(array($this->netConfigDNS->macAddress));
            $tmp->set_type(TRIGGERED_EVENT);
            $o_queue = new supportDaemon();
            if (!$o_queue->append($tmp)) {
              msg_dialog::display(_("Service infrastructure"), msgPool::siError($o_queue->get_error()), ERROR_DIALOG);
            }
          }
        } else {
          msg_dialog::display(_("Event error"),
                      sprintf(_("Event '%s' is not available!"), $action), ERROR_DIALOG);
        }
      }
    }
  }

  function renderFormInput ()
  {
    $id = $this->getHtmlId();
    $disabled = ($this->plugin->currently_installing?' disabled="disabled"':'');
    $button = '<input type="submit" name="'.$id.'_action" id="'.$id.'_action" value="{t}Execute{/t}"'.$disabled.'/>';
    return parent::renderFormInput().$this->renderAcl($button);
  }

  function updateChoices()
  {
    /* Check if workstation is online */
    $actions = array( // TODO : remove these action if there is no deployment plugin to use them (FAI, OPSI, …)
      ''          => '',
      'reinstall' => _('Reinstall'),
      'update'    => _('Software update'),
      'memcheck'  => _('Memory test')
    );
    if ($this->plugin->ping()) {
      $actions['halt']    = _('Switch off');
      $actions['reboot']  = _('Reboot');
      $actions['rescan']  = _('Rescan hardware');
    } else {
      $actions['wakeup']  = _('Wake up');
    }
    $this->setChoices(array_keys($actions), array_values($actions));
  }
}

class workgeneric extends simplePlugin
{
  var $mainTab = TRUE;

  var $netConfigDNS;

  var $objectclasses = array("top", "gotoWorkstation", "GOhard");

  var $fai_activated = FALSE;

  var $member_of_ogroup     = FALSE;
  var $currently_installing = FALSE;
  var $sdaemon_available    = FALSE;

  var $preInitAttributes = array('network' => array('networkSettings'));

  /*!
   *  \brief The main function : information about attributes
   */
  static function getAttributesInfo ()
  {
    return array(
      'main' => array(
        'name'  => _('Properties'),
        'attrs' => array(
          new StringAttribute (
            _('Workstation name'),
            _('The name of the workstation'),
            'cn',
            TRUE
          ),
          new StringAttribute (
            _('Description'),
            _('A short description of the workstation'),
            'description',
            FALSE
          ),
          new StringAttribute (
            _('Location'),
            _('The location of the workstation'),
            'l',
            FALSE
          ),
          new BaseSelectorAttribute(get_ou("workstationRDN")),
          new BooleanAttribute (
            _('Lock this workstation'),
            _('This will prevent the workstation from being reinstalled'),
            'gotoMode',
            FALSE,
            FALSE,
            '',
            'locked',
            'active'
          ),
        )
      ),
      'servers' => array(
        'name'  => _('Servers'),
        'icon'  => get_template_path("plugins/systems/images/select_server.png"),
        'attrs' => array(
          new SelectAttribute (
            _('Syslog server'),
            _('The syslog server the workstation should be using'),
            'gotoSyslogServer',
            FALSE
          ),
          new BooleanAttribute (
            _('Inherit NTP servers'),
            _('Inherit time server attributes from ogroup'),
            'inheritTimeServer',
            FALSE
          ),
          new SetAttribute (
            new SelectAttribute (
              _('NTP servers'),
              _('The NTP servers for the workstation'),
              'gotoNtpServer',
              FALSE
            )
          ),
          new BooleanAttribute (
            _('Inherit LDAP servers'),
            _('Inherit LDAP server attributes from ogroup'),
            'inheritLDAPServer',
            FALSE
          ),
          new OrderedArrayAttribute (
            new SelectAttribute (
              _('LDAP servers'),
              _('The LDAP servers for the workstation'),
              'gotoLdapServer',
              FALSE
            )
          ),
        )
      ),
      'actions' => array(
        'name' => _('Action'),
        'icon'  => get_template_path("images/rocket.png"),
        'attrs' => array(
          new ActionAttribute(
            _('Action'),
            _('Select action to execute for this terminal'),
            'FAIstate'
          ),
        )
      ),
      'network' => array(
        'name'      => _('Network settings'),
        'attrs'     => array(new NetworkSettingsAttribute()),
        'template'  => get_template_path('network_section.tpl', TRUE, dirname(__FILE__)),
        'icon'      => get_template_path('plugins/systems/images/network.png'),
      ),
    );
  }

  function __construct (&$config, $dn = NULL, $object = NULL)
  {
    $s_daemon = new supportDaemon();
    $this->sdaemon_available = $s_daemon->is_available();

    parent::__construct($config, $dn, $object);

    if (!isset($this->parent->by_object['ogroup'])) {
      $ldap = $this->config->get_ldap_link();
      $ldap->cd($this->config->current['BASE']);
      $ldap->search("(&(objectClass=gotoWorkstationTemplate)(member=".LDAP::prepare4filter($this->dn)."))", array("cn"));
      $this->member_of_ogroup = ($ldap->count() >= 1);
    }

    /* Check if this host is currently in installation process*/
    if ($this->sdaemon_available) {
      $this->currently_installing = $s_daemon->is_currently_installing($this->netConfigDNS->macAddress);
    }

    $this->attributesInfo['main']['attrs']['cn']->setUnique(TRUE);

    $this->attributesInfo['servers']['attrs']['inheritTimeServer']->setInLdap(FALSE);
    $this->attributesInfo['servers']['attrs']['inheritTimeServer']->setManagedAttributes(
      array(
        'erase' => array(
          TRUE => array(
            'servers' => array('gotoNtpServer'),
          )
        )
      )
    );

    $this->attributesInfo['servers']['attrs']['inheritLDAPServer']->setInLdap(FALSE);
    $this->attributesInfo['servers']['attrs']['inheritLDAPServer']->setManagedAttributes(
      array(
        'erase' => array(
          TRUE => array(
            'servers' => array('gotoLdapServer'),
          )
        )
      )
    );

    /* Create used ntp server array */
    $ntpServers = $this->attributesInfo['servers']['attrs']['gotoNtpServer']->getValue();
    /* Set inherit checkbox state */
    if ($this->member_of_ogroup && (in_array("default", $ntpServers) || (count($ntpServers) == 0))) {
      $this->attributesInfo['servers']['attrs']['inheritTimeServer']->setValue(TRUE);
      $this->attributesInfo['servers']['attrs']['gotoNtpServer']->setValue(array());
    } else {
      $this->attributesInfo['servers']['attrs']['inheritTimeServer']->setValue(FALSE);
    }

    /* Create used ldap server array */
    $ldapServers = $this->attributesInfo['servers']['attrs']['gotoLdapServer']->getValue();
    /* Set inherit checkbox state */
    if ($this->member_of_ogroup && (count($ldapServers) == 0)) {
      $this->attributesInfo['servers']['attrs']['inheritLDAPServer']->setValue(TRUE);
    } else {
      $this->attributesInfo['servers']['attrs']['inheritLDAPServer']->setValue(FALSE);
    }

    /* Disable inheritance if we're not in a group */
    if (!$this->member_of_ogroup) {
      $this->attributesInfo['servers']['attrs']['inheritTimeServer']->setDisabled(TRUE);
      $this->attributesInfo['servers']['attrs']['inheritTimeServer']->setVisible(FALSE);
      $this->attributesInfo['servers']['attrs']['inheritLDAPServer']->setDisabled(TRUE);
      $this->attributesInfo['servers']['attrs']['inheritLDAPServer']->setVisible(FALSE);
    }

    /* Create available ntp options */
    $tmp = array();
    foreach ($this->config->data['SERVERS']['NTP'] as $key => $server) {
      if ($server == "default") {
        continue;
      }
      $tmp[] = $server;
    }
    $this->attributesInfo['servers']['attrs']['gotoNtpServer']->attribute->setChoices($tmp);
    if (count($tmp) == 0) {
      $this->attributesInfo['servers']['attrs']['gotoNtpServer']->setDisabled(TRUE);
      $this->attributesInfo['servers']['attrs']['inheritTimeServer']->setValue(TRUE);
      $this->attributesInfo['servers']['attrs']['inheritTimeServer']->setDisabled(TRUE);
    }

    /* Create available ldap options */
    $tmp = array();
    foreach ($this->config->data['SERVERS']['LDAP'] as $dn => $data) {
      for ($i = 0; $i < $data['goLdapBase']['count']; $i++) {
        $tmp[] = $data["cn"][0].":".$data["goLdapBase"][$i];
      }
    }
    $this->attributesInfo['servers']['attrs']['gotoLdapServer']->attribute->setChoices($tmp);
    if (count($tmp) == 0) {
      $this->attributesInfo['servers']['attrs']['gotoLdapServer']->setDisabled(TRUE);
      $this->attributesInfo['servers']['attrs']['inheritLDAPServer']->setValue(TRUE);
      $this->attributesInfo['servers']['attrs']['inheritLDAPServer']->setDisabled(TRUE);
    }

    /* Create an array of all Syslog servers */
    $tmp        = $this->config->data['SERVERS']['SYSLOG'];
    $tmp_output = $tmp;
    if (($key = array_search("default", $tmp)) !== FALSE) {
      $tmp[$key] = "";
      if ($this->member_of_ogroup) {
        $tmp_output[$key] = _('inherited');
      }
    }
    $this->attributesInfo['servers']['attrs']['gotoSyslogServer']->setChoices($tmp, $tmp_output);
  }

  function ping()
  {
    if (!$this->sdaemon_available) {
      return FALSE;
    }
    $sdaemon = new supportDaemon();
    if (!$sdaemon->is_available()) {
      $this->sdaemon_available = FALSE;
      return FALSE;
    }
    return $sdaemon->ping($this->netConfigDNS->macAddress);
  }


  function set_acl_base($base)
  {
    plugin::set_acl_base($base);
    if (is_object($this->netConfigDNS)) {
      $this->netConfigDNS->set_acl_base($base);
    }
  }

  function set_acl_category($cat)
  {
    plugin::set_acl_category($cat);
    $this->netConfigDNS->set_acl_category($cat);
  }

  function remove_from_parent()
  {
    if (!$this->initially_was_account) {
      return;
    }

    if ($this->acl_is_removeable()) {
      $this->netConfigDNS->remove_from_parent();

      parent::remove_from_parent();

      /* Delete references to object groups */
      $ldap = $this->config->get_ldap_link();
      $ldap->cd($this->config->current['BASE']);
      $ldap->search("(&(objectClass=gosaGroupOfNames)(member=".LDAP::prepare4filter($this->dn)."))", array("cn"));
      while ($ldap->fetch()) {
        $og = new ogroup($this->config, $ldap->getDN());
        unset($og->member[$this->dn]);
        $og->save();
      }

      /* Remove all accessTo/trust dependencies */
      update_accessTo($this->cn, "");

      /* Clean queue form entries with this mac
       */
      if (class_available("supportDaemon") && tests::is_mac($this->netConfigDNS->orig_macAddress)) {
        $q = new supportDaemon();
        $q->clean_queue_from_mac($this->netConfigDNS->orig_macAddress);
      }
    }
  }

  /* Check supplied data */
  function check()
  {
    $message = parent::check();

    $activate = ($this->attributesInfo['main']['attrs']['gotoMode']->hasChanged() &&
                 $this->attributesInfo['main']['attrs']['gotoMode']->getValue());
    /* Only systems with a valid ldap handle can be activated
     */
    if ($activate) {
      if ((count($this->attributesInfo['servers']['attrs']['gotoLdapServer']->getValue()) == 0) &&
          !$this->attributesInfo['servers']['attrs']['inheritLDAPServer']->getValue()) {
        $message[] = _("A valid LDAP server assignement is missing!");
      }
    }

    return $message;
  }

  protected function ldap_save($cleanup = TRUE)
  {
    $activate = ($this->attributesInfo['main']['attrs']['gotoMode']->hasChanged() &&
                 $this->attributesInfo['main']['attrs']['gotoMode']->getValue());
    parent::ldap_save($cleanup);
    $this->netConfigDNS->cn = $this->cn;
    $this->netConfigDNS->save();

    if ($activate && $this->sdaemon_available) {
      /* Send installation activation
       */
      $events = DaemonEvent::get_event_types(SYSTEM_EVENT | HIDDEN_EVENT);
      $o_queue = new supportDaemon();
      if (isset($events['TRIGGERED']['DaemonEvent_installation_activation'])) {
        $evt = $events['TRIGGERED']['DaemonEvent_installation_activation'];
        $tmp = new $evt['CLASS_NAME']($this->config);
        $tmp->set_type(TRIGGERED_EVENT);
        $tmp->add_targets(array($this->netConfigDNS->macAddress));
        $o_queue->append($tmp);
        if ($o_queue->is_error()) {
          msg_dialog::display(_("Service infrastructure"), msgPool::siError($o_queue->get_error()), ERROR_DIALOG);
        }
      }
    }
  }

  function is_modal_dialog()
  {
    return (isset($this->netConfigDNS->dialog) && $this->netConfigDNS->dialog);
  }

  /* Return plugin informations for acl handling */
  static function plInfo()
  {
    return array(
      "plShortName"     => _("Generic"),
      "plDescription"   => _("Workstation generic"),
      "plSelfModify"    => FALSE,
      "plDepends"       => array(),
      "plPriority"      => 0,
      "plSection"       => array("administration"),
      "plCategory"      => array("workstation" => array("description"  => _("Workstation"),
                                                      "objectClass"  => "gotoWorkstation")),
      "plProvidedAcls"  => parent::generatePlProvidedAcls(self::getAttributesInfo())
    );
  }
}
?>