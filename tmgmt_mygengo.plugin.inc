<?php

/**
 * @file
 * Provides Gengo translation plugin controller.
 */

/**
 * Gengo translation plugin controller.
 */
class TMGMTMyGengoTranslatorPluginController extends TMGMTDefaultTranslatorPluginController {

  /**
   * If set it will be sent by job post action as a comment.
   *
   * @var string
   */
  protected $serviceComment;


  /**
   * Implements TMGMTDefaultTranslatorPluginController::getDefaultRemoteLanguagesMappings().
   */
  public function getDefaultRemoteLanguagesMappings() {
    return array(
      'zh-hans' => 'zh',
      'zh-hant' => 'zh-tw',
    );
  }

  /**
   * Sets comment to be sent to gengo service with job post request.
   *
   * @param string $comment
   */
  public function setServiceComment($comment) {
    $this->serviceComment = check_plain(trim($comment));
  }

  /**
   * Implements TMGMTTranslatorRejectDataItem::rejectForm()
   */
  public function rejectForm($form, &$form_state) {
    $form['message'] = array(
      '#markup' => '<div class="messages warning">' .
          t('By rejecting this item you will submit a new translate job to the Gengo translate service which will result in additional costs.') . '</div>',
    );
    $form['comment'] = array(
      '#type' => 'textarea',
      '#title' => t('Rejection comment'),
      '#description' => t('Provide a brief explanation that you actually rejected previous translation and state your reasons.'),
    );
    return $form;
  }

  /**
   * Implements TMGMTTranslatorPluginControllerInterface::isAvailable().
   */
  public function isAvailable(TMGMTTranslator $translator) {
    if ($translator->getSetting('api_public_key') && $translator->getSetting('api_private_key')) {
      return TRUE;
    }
    return FALSE;
  }

  /**
   * Implements TMGMTTranslatorPluginControllerInterface::requestTranslation().
   *
   * Here we will actually query source and get translations.
   */
  public function requestTranslation(TMGMTJob $job) {

    try {

      // Check if we have comment from user input and if yes, set it to be sent.
      if (!empty($job->settings['comment'])) {
        $this->setServiceComment($job->settings['comment']);
      }

      $response = $this->sendJob($job);

      // If we already receive jobs, process them.
      if (!empty($response->jobs)) {
        $this->processGengoJobsUponTranslationRequest($job, $response->jobs);
      }
      elseif(isset($response->order_id)) {
        $this->initGengoMapping($job, $response->order_id);
      }

      $job->submitted(t('Job has been submitted.'));
    }
    catch (TMGMTException $e) {
      watchdog_exception('tmgmt_mygengo', $e);
      $job->rejected('Job has been rejected with following error: @error',
        array('@error' => $e->getMessage()), 'error');
    }
  }

  /**
   * Implements TMGMTDefaultTranslatorPluginController::getSupportedRemoteLanguages().
   */
  function getSupportedRemoteLanguages(TMGMTTranslator $translator) {
    if (!empty($this->supportedRemoteLanguages)) {
      return $this->supportedRemoteLanguages;
    }

    try {
      $connector = new TMGMTGengoConnector($translator);
      foreach($connector->getLanguages() as $gengo_language) {
        $this->supportedRemoteLanguages[$gengo_language->lc] = $gengo_language->lc;
      }
    }
    catch (TMGMTException $e) {
      watchdog_exception('tmgmt', $e);
      drupal_set_message($e->getMessage(), 'error');
    }

    return $this->supportedRemoteLanguages;
  }

  /**
   * Implements TMGMTTranslatorPluginControllerInterface::getSupportedTargetLanguages().
   */
  public function getSupportedTargetLanguages(TMGMTTranslator $translator, $source_language) {
    $results = array();

    $connector = new TMGMTGengoConnector($translator);
    $response = $connector->getLanguages($translator->mapToRemoteLanguage($source_language));
    foreach ($response as $target) {
      $results[$translator->mapToLocalLanguage($target->lc)] = $translator->mapToLocalLanguage($target->lc);
    }

    return $results;
  }

  /**
   * Implements TMGMTDefaultTranslatorPluginController::canTranslate().
   */
  public function canTranslate(TMGMTTranslator $translator, TMGMTJob $job) {

    $target_lang = $this->mapToRemoteLanguage($translator, $job->target_language);
    $source_lang = $this->mapToRemoteLanguage($translator, $job->source_language);

    if ($this->isAvailable($translator) && array_key_exists($target_lang, $translator->getSupportedTargetLanguages($source_lang))) {
      return TRUE;
    }
    return FALSE;
  }

  /**
   * Will build and send a job to gengo service.
   *
   * @param TMGMTJob $job
   *   Job to be submitted for translation.
   * @param boolean $quote_only
   *   (Optional) Set to TRUE to only get a quote for the given job.
   *
   * @return mixed
   *   - Array of job objects returned from gengo.
   *   - Status object with order info.
   */
  public function sendJob(TMGMTJob $job, $quote_only = FALSE) {
    $data = tmgmt_flatten_data($job->getData());

    $translator = $job->getTranslator();
    $translations = array();
    $sources = array();
    $position = 0;

    foreach ($data as $key => $value) {
      if (isset($value['#translate']) && $value['#translate'] === FALSE) {
        continue;
      }

      if (!$quote_only) {
        // Detect duplicate source strings and add a mapping for them.
        if ($duplicate_key = array_search($value['#text'], $sources)) {

          // Only add the actual mapping in case we are not requesting a quote.
          if (!$quote_only) {
            // Get the job item.
            // @todo This is not very nice. We should possibly support this
            //   directly on the job.
            $tjiid = substr($key, 0, strpos($key, ']'));
            $job_item = tmgmt_job_item_load($tjiid);
            $job_item->addRemoteMapping($key, $duplicate_key);
          }
          continue;
        }
      }

      // Keep track of source texts for easy lookup.
      $sources[$key] = $value['#text'];

      $translations[$job->tjid . '][' . $key] = array(
        'type' => 'text',
        'slug' => tmgmt_data_item_label($value),
        'body_src' => $value['#text'],
        'lc_src' => $translator->mapToRemoteLanguage($job->source_language),
        'lc_tgt' => $translator->mapToRemoteLanguage($job->target_language),
        'tier' => $job->settings['quality'],
        'callback_url' => url('tmgmt_mygengo_callback', array('absolute' => TRUE)),
        'custom_data' => $job->tjid . '][' . $key,
        'position' => $position++,
        'auto_approve' => (int) $translator->getSetting('mygengo_auto_approve'),
        'use_preferred' => (int) $translator->getSetting('use_preferred'),
      );

      if (!empty($this->serviceComment)) {
        $translations[$job->tjid . '][' . $key]['comment'] = $this->serviceComment;
      }
    }

    $connector = new TMGMTGengoConnector($job->getTranslator());
    if ($quote_only) {
      return $connector->getQuote($translations);
    }
    else {
      return $connector->submitJob($translations);
    }
  }

  /**
   * Receives and stores a translation returned by Gengo.
   *
   * @param TMGMTJob $job
   *   Job for which to receive translations.
   * @param array $keys
   *   Data keys for data item which will be updated with translation.
   * @param array $data
   *   Translated data received from gengo.
   */
  public function saveTranslation(TMGMTJob $job, $keys, $data) {
    if ($data->status == 'approved' || $data->status == 'reviewable') {
      $keys = tmgmt_ensure_keys_array($keys);
      $job->addTranslatedData(array('#text' => $data->body_tgt), $keys);

      // Look for duplicated strings that were saved with a mapping to this key.
      // @todo Allow additional conditions in TmgmtJob::getRemoteMappings()?
      $query = new EntityFieldQuery();
      $query->entityCondition('entity_type', 'tmgmt_remote');
      $query->propertyCondition('tjid', $job->tjid);
      $query->propertyCondition('remote_identifier_1', tmgmt_ensure_keys_string($keys));

      $result = $query->execute();

      if (isset($result['tmgmt_remote'])) {
        $remote_mappings = entity_load('tmgmt_remote', array_keys($result['tmgmt_remote']));
        // If we found any mappings, also add the translation for those.
        foreach ($remote_mappings as $remote_mapping) {
          $job->addTranslatedData(array('#text' => $data->body_tgt), $remote_mapping->data_item_key);
        }
      }
    }
  }

  /**
   * Will process remote jobs upon translation request.
   *
   * This deals with following:
   *   - Creates mappings of local data items to remote gengo jobs.
   *   - Saves translation in case it has been already received.
   *   - Deals with duplicate translations.
   *
   * @param TMGMTJob $job
   *   Local job that is going to be processed.
   * @param array $response_jobs
   *   List of gengo jobs received.
   */
  protected function processGengoJobsUponTranslationRequest(TMGMTJob $job, $response_jobs) {

    $mappings = array();

    foreach ($response_jobs as $key => $response_job) {

      // Duplicate content has been submitted.
      if (isset($response_job->duplicate)) {
        // @todo We need to handle duplicates in some way as client will not
        //   receive translation for this item.
        continue;
      }

      // For machine translations the job is not wrapped in another object
      // however for human translations it is. So try to cope with this
      // gengo system variety.
      if (!isset($response_job->custom_data)) {
        $response_job = reset($response_job);
      }

      // In case we receive an existing translation the array offset IS also
      // the data kay, and we ought to use it as the object custom data is not
      // updated. However this is not always the case and in some case we
      // receive numeric keys, so try to set some custom data to increase the
      // chance of matching the job.
      if (is_numeric($key)) {
        $key = $response_job->custom_data;
      }

      $this->saveTranslation($job, array_slice(tmgmt_ensure_keys_array($key), 1), $response_job);

      $mappings[] = array(
        'tjid' => $job->tjid,
        // Yes, this is not a joke, they really return string value "NULL" in
        // case of a machine translation.
        'gjid' => $response_job->job_id == 'NULL' ? 0 : $response_job->job_id,
        'data_item_key' => tmgmt_ensure_keys_string($key),
      );
    }

    // For jobs received upon submission we create gengo mappings differently
    // than in case of regular translation cycle.
    $this->saveGengoMappings($mappings);
  }

  ///// Mappings - will be removed/refactored

  /**
   * Saves mapping with tmgmt data item and gengo job.
   *
   * @param array $mappings
   *   Array of associated mappings data to save in format:
   *   - tjid - Translation job id
   *   - gjid - Gengo translation job id
   *   - key - Data item key
   */
  public function saveGengoMappings(array $mappings) {
    foreach ($mappings as $mapping) {
      $select = db_select('tmgmt_mygengo_mappings', 'm')->fields('m', array('tjid'));
      $select->condition('tjid', $mapping['tjid']);
      $select->condition('gjid', $mapping['gjid']);

      if ($select->execute()->fetchField()) {
        drupal_write_record('tmgmt_mygengo_mappings', $mapping, array('tjid', 'gjid'));
      }
      else {
        drupal_write_record('tmgmt_mygengo_mappings', $mapping);
      }
    }
  }

  /**
   * Creates a placeholder record in the mapping table.
   *
   * The idea here is not to introduce additional storage to temporarily store
   * gegngo order id before we get gengo job ids.
   *
   * @param TMGMTJob $job
   *   Job for which to initiate mappings with remote jobs.
   * @param int $gorder_id
   *   Gengo job id.
   */
  protected function initGengoMapping(TMGMTJob $job, $gorder_id) {
    $data = array(
      'tjid' => $job->tjid,
      'gorder_id' => $gorder_id,
    );
    drupal_write_record('tmgmt_mygengo_mappings', $data);
  }

  /**
   * Maps TMGMT job data items to gengo jobs.
   *
   * @param TMGMTJob $job
   *   Job that will be mapped to remote jobs.
   */
  public function mapGengoJobs(TMGMTJob $job) {

    $select = db_select('tmgmt_mygengo_mappings', 'm');
    $select->addExpression('COUNT(*)');
    $select->condition('tjid', $job->tjid);
    $select->condition('gjid', '0', '>');
    $mapped_jobs_count = $select->execute()->fetchField();

    // Search for placeholder item.
    $select = db_select('tmgmt_mygengo_mappings', 'm');
    $select->fields('m', array('gorder_id'));
    $select->condition('tjid', $job->tjid);
    $select->condition('gjid', '0');

    $connector = new TMGMTGengoConnector($job->getTranslator());

    // If we have a placeholder we want to fetch gengo job ids.
    if ($gorder_id = $select->execute()->fetchField()) {

      $response = NULL;

      try {
        $response = $connector->getOrder($gorder_id);
      }
      catch (TMGMTException $e) {
        watchdog_exception('tmgmt_mygengo', $e);
        drupal_set_message($e->getMessage(), 'error');
        return;
      }

      // No jobs yet created at Gengo side. Nothing to do.
      if ($response->order->jobs_queued == $response->order->total_jobs) {
        return;
      }

      $gengo_job_ids = array();
      if (!empty($response->order->jobs_available)) {
        $gengo_job_ids += $response->order->jobs_available;
      }
      if (!empty($response->order->jobs_pending)) {
        $gengo_job_ids += $response->order->jobs_pending;
      }
      if (!empty($response->order->jobs_reviewable)) {
        $gengo_job_ids += $response->order->jobs_reviewable;
      }
      if (!empty($response->order->jobs_approved)) {
        $gengo_job_ids += $response->order->jobs_approved;
      }
      if (!empty($response->order->jobs_revising)) {
        $gengo_job_ids += $response->order->jobs_revising;
      }

      // Remove the placeholder record if we got mappings for all the jobs.
      if ($mapped_jobs_count == $response->order->total_jobs) {
        db_delete('tmgmt_mygengo_mappings')->condition('gorder_id', $gorder_id)->execute();
      }

      $mappings = array();
      foreach ($gengo_job_ids as $gengo_job_id) {
        $mappings[] = array(
          'tjid' => $job->tjid,
          'gjid' => $gengo_job_id,
          'gorder_id' => $gorder_id,
        );
      }
      // Add intial records into the table
      $this->saveGengoMappings($mappings);
    }

    // Get gengo jobs for existing gengo job ids.
    $response = $connector->getJobs($this->getGengoJobIds($job->tjid));

    // Gengo did not provide any mapping data, do nothing.
    // This only happens in the case that Gengo is unreachable.
    if (empty($response->jobs)) {
      return;
    }

    // update mappings and save any translations
    $mappings = array();
    foreach ($response->jobs as $key => $response_job) {
      $mappings[] = array(
        'tjid' => $job->tjid,
        'gjid' => $response_job->job_id,
        'data_item_key' => $response_job->custom_data,
      );

      if (is_numeric($key)) {
        $key = $response_job->custom_data;
      }
      $key = array_slice(tmgmt_ensure_keys_array($key), 1);
      $this->saveTranslation($job, $key, $response_job);
    }
    // Update mapping records
    $this->saveGengoMappings($mappings);
  }

  /**
   * Gets gengo job ids for a tmgmt job.
   *
   * @param int $tjid
   *   TMGMT Job id.
   *
   * @return array
   *   Array of gengo job ids.
   */
  public function getGengoJobIds($tjid) {
    $select = db_select('tmgmt_mygengo_mappings', 'm')->fields('m', array('gjid'));
    $select->condition('tjid', $tjid);
    $select->condition('gjid', '0', '>');

    return $select->execute()->fetchCol();
  }

  /**
   * Gets gengo mappings not being mapped to local data items.
   *
   * @param int $tjid
   *   Job id for which to get mapping data.
   *
   * @return array
   *   Array of mapping records keyd by gengo job ids.
   */
  public function getMappedGengoData($tjid) {
    $select = db_select('tmgmt_mygengo_mappings', 'm')->fields('m');
    $select->condition('tjid', $tjid);
    $select->condition('data_item_key', '', '!=');

    return $select->execute()->fetchAllAssoc('gjid');
  }
}
