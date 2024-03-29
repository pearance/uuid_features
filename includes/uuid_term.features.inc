<?php
/**
 * @file
 * Features hooks for the uuid_term features component.
 */

/**
 * Implements hook_features_export_options().
 */
function uuid_term_features_export_options() {
  $options = array();

  $query = 'SELECT t.tid, t.name, v.name AS vname, t.uuid
    FROM {taxonomy_term_data} t
    INNER JOIN {taxonomy_vocabulary} v ON t.vid = v.vid
    ORDER BY v.name ASC, t.name ASC';
  $results = db_query($query);
  foreach ($results as $term) {
    $options[$term->uuid] = $term->vname . ' - ' . $term->name;
  }

  return $options;
}

/**
 * Implements hook_features_export().
 */
function uuid_term_features_export($data, &$export, $module_name = '') {
  $export['dependencies']['taxonomy'] = 'taxonomy';
  $export['dependencies']['uuid'] = 'uuid';
  $export['dependencies']['uuid_features'] = 'uuid_features';

  foreach ($data as $uuid) {
    uuid_term_features_get_dependencies($export, $uuid);
  }
  return array();
}

/**
 * Adds terms and its dependencies to the export.
 *
 * Parents and term references are handled recrusively, other references are not
 * yet supported.
 */
function uuid_term_features_get_dependencies(&$export, $uuid) {
  $terms = entity_uuid_load('taxonomy_term', array($uuid));
  if (count($terms)) {
    $term = reset($terms);
    $export['features']['uuid_term'][$uuid] = $uuid;
    $export['features']['taxonomy'][$term->vocabulary_machine_name] = $term->vocabulary_machine_name;
    // Recursively add all parents and the references of the parents.
    foreach (taxonomy_get_parents($term->tid) as $parent) {
      if (!in_array($parent->uuid, $export['features']['uuid_term'])) {
        uuid_term_features_get_dependencies($export, $parent->uuid);
      }
    }
    // Get term references.
    $instances = field_info_instances('taxonomy_term', $term->vocabulary_machine_name);
    foreach ($instances as $field_name => $instance) {
      $field = field_info_field($field_name);
      if ($field['type'] == 'taxonomy_term_reference') {
        if (isset($term->$field_name)) {
          foreach ($term->$field_name as $lang => $values) {
            foreach ($values as $value) {
              // $value['tid'] already contains the UUID.
              if (!in_array($value['tid'], $export['features']['uuid_term'])) {
                uuid_term_features_get_dependencies($export, $value['tid']);
              }
            }
          }
        }
      }
    }
  }
}

/**
 * Implements hook_features_export_render().
 */
function uuid_term_features_export_render($module = 'foo', $data) {
  $translatables = $code = array();

  $code[] = '  $terms = array();';
  $code[] = '';
  foreach ($data as $uuid) {
    // @todo reset = TRUE as otherwise references (parent, fields) were destroyed.
    $terms = entity_uuid_load('taxonomy_term', array($uuid), array(), TRUE);
    if (!count($terms)) {
      continue;
    }
    $export = reset($terms);

    // Do not export ids.
    unset($export->vid);
    unset($export->tid);
    // No need to export the rdf mapping.
    unset($export->rdf_mapping);
    $code[] = '  $terms[] = ' . features_var_export($export, '  ') . ';';
  }

  if (!empty($translatables)) {
    $code[] = features_translatables_export($translatables, '  ');
  }

  $code[] = '  return $terms;';
  $code = implode("\n", $code);
  return array('uuid_features_default_terms' => $code);
}

/**
 * Implements hook_features_revert().
 */
function uuid_term_features_revert($module) {
  uuid_term_features_rebuild($module);
}

/**
 * Implements hook_features_rebuild().
 * Rebuilds terms based on UUID from code defaults.
 */
function uuid_term_features_rebuild($module) {
  // Import the vocabularies first.
  taxonomy_features_rebuild($module);

  $terms = module_invoke($module, 'uuid_features_default_terms');
  if (!empty($terms)) {
    // Verify that term objects is saved before any references are resolved.
    foreach ($terms as $data) {
      $existing = entity_get_id_by_uuid('taxonomy_term', array($data['uuid']));
      if (!count($existing)) {
        $voc = taxonomy_vocabulary_machine_name_load($data['vocabulary_machine_name']);
        // Only save the term, if the corresponding vocabulary already exisits.
        if ($voc) {
          $term = new stdClass;
          $term->uuid = $data['uuid'];
          $term->vid = $voc->vid;
          $term->name = $data['name'];
          taxonomy_term_save($term);
        } 
      }
    }
    // Save all other data and resolve references.
    foreach ($terms as $data) {
      $term = (object) $data;
      $voc = taxonomy_vocabulary_machine_name_load($term->vocabulary_machine_name);
      if ($voc) {
        $term->vid = $voc->vid;
        entity_uuid_save('taxonomy_term', $term);
      }
    }
  }
}
